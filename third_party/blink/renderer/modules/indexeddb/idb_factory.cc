/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/modules/indexeddb/idb_factory.h"

#include <memory>
#include <utility>

#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/self_owned_associated_receiver.h"
#include "third_party/blink/public/mojom/feature_observer/feature_observer.mojom-blink.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom-blink.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_binding_for_modules.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_idb_database_info.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/probe/async_task_context.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/workers/worker_global_scope.h"
#include "third_party/blink/renderer/modules/indexed_db_names.h"
#include "third_party/blink/renderer/modules/indexeddb/idb_database.h"
#include "third_party/blink/renderer/modules/indexeddb/idb_key.h"
#include "third_party/blink/renderer/modules/indexeddb/idb_name_and_version.h"
#include "third_party/blink/renderer/modules/indexeddb/idb_tracing.h"
#include "third_party/blink/renderer/modules/indexeddb/web_idb_callbacks.h"
#include "third_party/blink/renderer/modules/indexeddb/web_idb_callbacks_impl.h"
#include "third_party/blink/renderer/modules/indexeddb/web_idb_transaction.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

namespace {

class WebIDBGetDBNamesCallbacksImpl : public WebIDBCallbacks {
 public:
  explicit WebIDBGetDBNamesCallbacksImpl(
      ScriptPromiseResolver* promise_resolver)
      : promise_resolver_(promise_resolver) {
    async_task_context_.Schedule(
        ExecutionContext::From(promise_resolver_->GetScriptState()),
        indexed_db_names::kIndexedDB);
  }

  ~WebIDBGetDBNamesCallbacksImpl() override {
    if (!promise_resolver_)
      return;

    auto* script_state = promise_resolver_->GetScriptState();
    if (!script_state->ContextIsValid())
      return;

    async_task_context_.Cancel();
    promise_resolver_->Reject(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kUnknownError,
        "An unexpected shutdown occured before the "
        "databases() promise could be resolved"));
  }

  void SetState(base::WeakPtr<WebIDBCursor> cursor,
                int64_t transaction_id) override {}

  void Error(mojom::blink::IDBException code, const String& message) override {
    if (!promise_resolver_)
      return;

    probe::AsyncTask async_task(
        ExecutionContext::From(promise_resolver_->GetScriptState()),
        &async_task_context_, "error");
    promise_resolver_->Reject(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kUnknownError,
        "The databases() promise was rejected."));
  }

  void SuccessNamesAndVersionsList(
      Vector<mojom::blink::IDBNameAndVersionPtr> names_and_versions) override {
    if (!promise_resolver_)
      return;
    DCHECK(!async_task_.has_value());

    HeapVector<Member<IDBDatabaseInfo>> name_and_version_list;
    name_and_version_list.ReserveInitialCapacity(name_and_version_list.size());
    for (const mojom::blink::IDBNameAndVersionPtr& name_version :
         names_and_versions) {
      const IDBNameAndVersion idb_name_and_version(name_version->name,
                                                   name_version->version);
      IDBDatabaseInfo* idb_info = IDBDatabaseInfo::Create();
      idb_info->setName(name_version->name);
      idb_info->setVersion(name_version->version);
      name_and_version_list.push_back(idb_info);
    }

    async_task_.emplace(
        ExecutionContext::From(promise_resolver_->GetScriptState()),
        &async_task_context_, "success");
    promise_resolver_->Resolve(name_and_version_list);
    // Note: Resolve may cause |this| to be deleted.  async_task_ will be
    // completed in the destructor.
  }

  void SuccessCursor(
      mojo::PendingAssociatedRemote<mojom::blink::IDBCursor> cursor_info,
      std::unique_ptr<IDBKey> key,
      std::unique_ptr<IDBKey> primary_key,
      absl::optional<std::unique_ptr<IDBValue>> optional_value) override {
    NOTREACHED();
  }

  void SuccessCursorPrefetch(
      Vector<std::unique_ptr<IDBKey>> keys,
      Vector<std::unique_ptr<IDBKey>> primary_keys,
      Vector<std::unique_ptr<IDBValue>> values) override {
    NOTREACHED();
  }

  void SuccessDatabase(
      mojo::PendingAssociatedRemote<mojom::blink::IDBDatabase> pending_backend,
      const IDBDatabaseMetadata& metadata) override {
    NOTREACHED();
  }

  void SuccessKey(std::unique_ptr<IDBKey> key) override { NOTREACHED(); }

  void SuccessValue(mojom::blink::IDBReturnValuePtr return_value) override {
    NOTREACHED();
  }

  void SuccessArray(Vector<mojom::blink::IDBReturnValuePtr> values) override {
    NOTREACHED();
  }

  void SuccessInteger(int64_t value) override { NOTREACHED(); }

  void Success() override { NOTREACHED(); }

  void SuccessCursorContinue(
      std::unique_ptr<IDBKey> key,
      std::unique_ptr<IDBKey> primary_key,
      absl::optional<std::unique_ptr<IDBValue>> value) override {
    NOTREACHED();
  }

  void ReceiveGetAllResults(
      bool key_only,
      mojo::PendingReceiver<mojom::blink::IDBDatabaseGetAllResultSink> receiver)
      override {
    NOTREACHED();
  }

  void Blocked(int64_t old_version) override { NOTREACHED(); }

  void UpgradeNeeded(
      mojo::PendingAssociatedRemote<mojom::blink::IDBDatabase> pending_database,
      int64_t old_version,
      mojom::IDBDataLoss data_loss,
      const String& data_loss_message,
      const IDBDatabaseMetadata& metadata) override {
    NOTREACHED();
  }

  void DetachRequestFromCallback() override { NOTREACHED(); }

 private:
  probe::AsyncTaskContext async_task_context_;
  absl::optional<probe::AsyncTask> async_task_;
  Persistent<ScriptPromiseResolver> promise_resolver_;
};

}  // namespace

static const char kPermissionDeniedErrorMessage[] =
    "The user denied permission to access the database.";

IDBFactory::IDBFactory() = default;
IDBFactory::~IDBFactory() = default;

static bool IsContextValid(ExecutionContext* context) {
  if (auto* window = DynamicTo<LocalDOMWindow>(context))
    return window->GetFrame();
  DCHECK(context->IsWorkerGlobalScope());
  return true;
}

void IDBFactory::SetFactoryForTesting(
    mojo::Remote<mojom::blink::IDBFactory> factory) {
  factory_ = std::move(factory);
}

mojo::Remote<mojom::blink::IDBFactory>& IDBFactory::GetFactory(
    ExecutionContext* execution_context) {
  if (!factory_) {
    mojo::PendingRemote<mojom::blink::IDBFactory> factory;
    execution_context->GetBrowserInterfaceBroker().GetInterface(
        factory.InitWithNewPipeAndPassReceiver());

    mojo::PendingRemote<mojom::blink::FeatureObserver> feature_observer;
    execution_context->GetBrowserInterfaceBroker().GetInterface(
        feature_observer.InitWithNewPipeAndPassReceiver());

    task_runner_ = execution_context->GetTaskRunner(TaskType::kDatabaseAccess);
    factory_.Bind(std::move(factory), task_runner_);
    feature_observer_.Bind(std::move(feature_observer), task_runner_);
  }
  return factory_;
}

ScriptPromise IDBFactory::GetDatabaseInfo(ScriptState* script_state,
                                          ExceptionState& exception_state) {
  // The BlinkIDL definition for GetDatabaseInfo() already has a [Measure]
  // attribute, so the kIndexedDBRead use counter must be explicitly updated.
  UseCounter::Count(ExecutionContext::From(script_state),
                    WebFeature::kIndexedDBRead);

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);

  if (!IsContextValid(ExecutionContext::From(script_state))) {
    resolver->Reject();
    return resolver->Promise();
  }

  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    exception_state.ThrowSecurityError(
        "Access to the IndexedDB API is denied in this context.");
    resolver->Reject();
    return resolver->Promise();
  }

  if (!AllowIndexedDB(script_state)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kUnknownError,
                                      kPermissionDeniedErrorMessage);
    resolver->Reject();
    return resolver->Promise();
  }

  auto callbacks = std::make_unique<WebIDBGetDBNamesCallbacksImpl>(resolver);
  callbacks->SetState(nullptr, WebIDBCallbacksImpl::kNoTransaction);
  GetFactory(ExecutionContext::From(script_state))
      ->GetDatabaseInfo(GetCallbacksProxy(std::move(callbacks)));
  return resolver->Promise();
}

void IDBFactory::GetDatabaseInfo(
    ScriptState* script_state,
    std::unique_ptr<mojom::blink::IDBCallbacks> callbacks) {
  // TODO(jsbell): Used only by inspector; remove unneeded checks/exceptions?
  if (!IsContextValid(ExecutionContext::From(script_state))) {
    return;
  }

  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    callbacks->Error(mojom::blink::IDBException::kAbortError,
                     "Access to the IndexedDB API is denied in this context.");
    return;
  }

  if (!AllowIndexedDB(script_state)) {
    callbacks->Error(mojom::blink::IDBException::kUnknownError,
                     kPermissionDeniedErrorMessage);
    return;
  }

  mojo::PendingAssociatedRemote<mojom::blink::IDBCallbacks> pending_callbacks;
  mojo::MakeSelfOwnedAssociatedReceiver(
      std::move(callbacks),
      pending_callbacks.InitWithNewEndpointAndPassReceiver());

  GetFactory(ExecutionContext::From(script_state))
      ->GetDatabaseInfo(std::move(pending_callbacks));
}

IDBOpenDBRequest* IDBFactory::open(ScriptState* script_state,
                                   const String& name,
                                   uint64_t version,
                                   ExceptionState& exception_state) {
  if (!version) {
    exception_state.ThrowTypeError("The version provided must not be 0.");
    return nullptr;
  }
  return OpenInternal(script_state, name, version, exception_state);
}

IDBOpenDBRequest* IDBFactory::OpenInternal(ScriptState* script_state,
                                           const String& name,
                                           int64_t version,
                                           ExceptionState& exception_state) {
  IDB_TRACE1("IDBFactory::open", "name", name.Utf8());
  IDBRequest::AsyncTraceState metrics("IDBFactory::open");
  DCHECK(version >= 1 || version == IDBDatabaseMetadata::kNoVersion);
  if (!IsContextValid(ExecutionContext::From(script_state)))
    return nullptr;
  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    exception_state.ThrowSecurityError(
        "access to the Indexed Database API is denied in this context.");
    return nullptr;
  }

  if (ExecutionContext::From(script_state)->GetSecurityOrigin()->IsLocal()) {
    UseCounter::Count(ExecutionContext::From(script_state),
                      WebFeature::kFileAccessedDatabase);
  }

  int64_t transaction_id = IDBDatabase::NextTransactionId();

  auto& factory = GetFactory(ExecutionContext::From(script_state));

  auto transaction_backend = std::make_unique<WebIDBTransaction>(
      ExecutionContext::From(script_state)
          ->GetTaskRunner(TaskType::kDatabaseAccess),
      transaction_id);
  mojo::PendingAssociatedReceiver<mojom::blink::IDBTransaction>
      transaction_receiver = transaction_backend->CreateReceiver();
  mojo::PendingAssociatedRemote<mojom::blink::IDBDatabaseCallbacks>
      callbacks_remote;
  auto* request = MakeGarbageCollected<IDBOpenDBRequest>(
      script_state, callbacks_remote.InitWithNewEndpointAndPassReceiver(),
      std::move(transaction_backend), transaction_id, version,
      std::move(metrics), GetObservedFeature());

  if (!AllowIndexedDB(script_state)) {
    request->HandleResponse(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kUnknownError, kPermissionDeniedErrorMessage));
    return request;
  }

  auto callbacks = request->CreateWebCallbacks();
  callbacks->SetState(nullptr, WebIDBCallbacksImpl::kNoTransaction);

  factory->Open(GetCallbacksProxy(std::move(callbacks)),
                std::move(callbacks_remote), name, version,
                std::move(transaction_receiver), transaction_id);
  return request;
}

IDBOpenDBRequest* IDBFactory::open(ScriptState* script_state,
                                   const String& name,
                                   ExceptionState& exception_state) {
  return OpenInternal(script_state, name, IDBDatabaseMetadata::kNoVersion,
                      exception_state);
}

IDBOpenDBRequest* IDBFactory::deleteDatabase(ScriptState* script_state,
                                             const String& name,
                                             ExceptionState& exception_state) {
  return DeleteDatabaseInternal(script_state, name, exception_state,
                                /*force_close=*/false);
}

IDBOpenDBRequest* IDBFactory::CloseConnectionsAndDeleteDatabase(
    ScriptState* script_state,
    const String& name,
    ExceptionState& exception_state) {
  // TODO(jsbell): Used only by inspector; remove unneeded checks/exceptions?
  return DeleteDatabaseInternal(script_state, name, exception_state,
                                /*force_close=*/true);
}

IDBOpenDBRequest* IDBFactory::DeleteDatabaseInternal(
    ScriptState* script_state,
    const String& name,
    ExceptionState& exception_state,
    bool force_close) {
  IDB_TRACE1("IDBFactory::deleteDatabase", "name", name.Utf8());
  IDBRequest::AsyncTraceState metrics("IDBFactory::deleteDatabase");
  if (!IsContextValid(ExecutionContext::From(script_state)))
    return nullptr;
  if (!ExecutionContext::From(script_state)
           ->GetSecurityOrigin()
           ->CanAccessDatabase()) {
    exception_state.ThrowSecurityError(
        "access to the Indexed Database API is denied in this context.");
    return nullptr;
  }

  if (ExecutionContext::From(script_state)->GetSecurityOrigin()->IsLocal()) {
    UseCounter::Count(ExecutionContext::From(script_state),
                      WebFeature::kFileAccessedDatabase);
  }

  auto& factory = GetFactory(ExecutionContext::From(script_state));

  auto* request = MakeGarbageCollected<IDBOpenDBRequest>(
      script_state,
      /*callbacks_receiver=*/mojo::NullAssociatedReceiver(),
      /*IDBTransactionAssociatedPtr=*/nullptr, 0,
      IDBDatabaseMetadata::kDefaultVersion, std::move(metrics),
      GetObservedFeature());

  if (!AllowIndexedDB(script_state)) {
    request->HandleResponse(MakeGarbageCollected<DOMException>(
        DOMExceptionCode::kUnknownError, kPermissionDeniedErrorMessage));
    return request;
  }

  auto callbacks = request->CreateWebCallbacks();
  callbacks->SetState(nullptr, WebIDBCallbacksImpl::kNoTransaction);
  factory->DeleteDatabase(GetCallbacksProxy(std::move(callbacks)), name,
                          force_close);
  return request;
}

int16_t IDBFactory::cmp(ScriptState* script_state,
                        const ScriptValue& first_value,
                        const ScriptValue& second_value,
                        ExceptionState& exception_state) {
  const std::unique_ptr<IDBKey> first =
      ScriptValue::To<std::unique_ptr<IDBKey>>(script_state->GetIsolate(),
                                               first_value, exception_state);
  if (exception_state.HadException())
    return 0;
  DCHECK(first);
  if (!first->IsValid()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return 0;
  }

  const std::unique_ptr<IDBKey> second =
      ScriptValue::To<std::unique_ptr<IDBKey>>(script_state->GetIsolate(),
                                               second_value, exception_state);
  if (exception_state.HadException())
    return 0;
  DCHECK(second);
  if (!second->IsValid()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kDataError,
                                      IDBDatabase::kNotValidKeyErrorMessage);
    return 0;
  }

  return static_cast<int16_t>(first->Compare(second.get()));
}

bool IDBFactory::AllowIndexedDB(ScriptState* script_state) {
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  DCHECK(execution_context->IsContextThread());
  SECURITY_DCHECK(execution_context->IsWindow() ||
                  execution_context->IsWorkerGlobalScope());
  if (auto* window = DynamicTo<LocalDOMWindow>(execution_context)) {
    LocalFrame* frame = window->GetFrame();
    if (!frame)
      return false;
    if (auto* settings_client = frame->GetContentSettingsClient()) {
      // This triggers a sync IPC.
      return settings_client->AllowStorageAccessSync(
          WebContentSettingsClient::StorageType::kIndexedDB);
    }
    return true;
  }

  WebContentSettingsClient* content_settings_client =
      To<WorkerGlobalScope>(execution_context)->ContentSettingsClient();
  if (!content_settings_client)
    return true;
  // This triggers a sync IPC.
  return content_settings_client->AllowStorageAccessSync(
      WebContentSettingsClient::StorageType::kIndexedDB);
}

mojo::PendingAssociatedRemote<mojom::blink::IDBCallbacks>
IDBFactory::GetCallbacksProxy(std::unique_ptr<WebIDBCallbacks> callbacks_impl) {
  mojo::PendingAssociatedRemote<mojom::blink::IDBCallbacks> pending_callbacks;
  mojo::MakeSelfOwnedAssociatedReceiver(
      std::move(callbacks_impl),
      pending_callbacks.InitWithNewEndpointAndPassReceiver(), task_runner_);
  return pending_callbacks;
}

mojo::PendingRemote<mojom::blink::ObservedFeature>
IDBFactory::GetObservedFeature() {
  mojo::PendingRemote<mojom::blink::ObservedFeature> feature;
  feature_observer_->Register(
      feature.InitWithNewPipeAndPassReceiver(),
      mojom::blink::ObservedFeatureType::kIndexedDBConnection);
  return feature;
}

}  // namespace blink
