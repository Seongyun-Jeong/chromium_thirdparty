// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// AUTO-GENERATED BY generate_zip_test_tables.sh - DO NOT EDIT!!

#include "crazy_linker_zip_test_data.h"

namespace crazy {
namespace testing {

// An empty zip archive
const unsigned char empty_archive_zip[] = {
    0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned int empty_archive_zip_len = 22;

// A zip archive with a single file named 'hello_world.txt' that
// contains the bytes for 'Hello World Hello World\n' without
// compression.
const unsigned char hello_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0x86,
    0xa3, 0x4c, 0xfd, 0x95, 0x3b, 0x3f, 0x18, 0x00, 0x00, 0x00, 0x18, 0x00,
    0x00, 0x00, 0x0f, 0x00, 0x1c, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x5f,
    0x77, 0x6f, 0x72, 0x6c, 0x64, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x09,
    0x00, 0x03, 0xb0, 0x22, 0xeb, 0x5a, 0xb0, 0x22, 0xeb, 0x5a, 0x75, 0x78,
    0x0b, 0x00, 0x01, 0x04, 0xab, 0x6f, 0x00, 0x00, 0x04, 0x53, 0x5f, 0x01,
    0x00, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64,
    0x20, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64,
    0x0a, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03, 0x0a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xd4, 0x86, 0xa3, 0x4c, 0xfd, 0x95, 0x3b, 0x3f, 0x18, 0x00, 0x00,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x68,
    0x65, 0x6c, 0x6c, 0x6f, 0x5f, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x2e, 0x74,
    0x78, 0x74, 0x55, 0x54, 0x05, 0x00, 0x03, 0xb0, 0x22, 0xeb, 0x5a, 0x75,
    0x78, 0x0b, 0x00, 0x01, 0x04, 0xab, 0x6f, 0x00, 0x00, 0x04, 0x53, 0x5f,
    0x01, 0x00, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x55, 0x00, 0x00, 0x00, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned int hello_zip_len = 204;

// The same zip archive, but with the file stored compressed.
const unsigned char hello_compressed_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0xd4, 0x86,
    0xa3, 0x4c, 0xfd, 0x95, 0x3b, 0x3f, 0x11, 0x00, 0x00, 0x00, 0x18, 0x00,
    0x00, 0x00, 0x0f, 0x00, 0x1c, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x5f,
    0x77, 0x6f, 0x72, 0x6c, 0x64, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x09,
    0x00, 0x03, 0xb0, 0x22, 0xeb, 0x5a, 0xb0, 0x22, 0xeb, 0x5a, 0x75, 0x78,
    0x0b, 0x00, 0x01, 0x04, 0xab, 0x6f, 0x00, 0x00, 0x04, 0x53, 0x5f, 0x01,
    0x00, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x08, 0xcf, 0x2f, 0xca, 0x49,
    0x51, 0xf0, 0x40, 0xb0, 0xb9, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03,
    0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0xd4, 0x86, 0xa3, 0x4c, 0xfd, 0x95,
    0x3b, 0x3f, 0x11, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x0f, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa4, 0x81,
    0x00, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x5f, 0x77, 0x6f,
    0x72, 0x6c, 0x64, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x05, 0x00, 0x03,
    0xb0, 0x22, 0xeb, 0x5a, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xab, 0x6f,
    0x00, 0x00, 0x04, 0x53, 0x5f, 0x01, 0x00, 0x50, 0x4b, 0x05, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x55, 0x00, 0x00, 0x00, 0x5a,
    0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned int hello_compressed_zip_len = 197;

// A zip archive with two uncompressed files under lib/test-abi/
// named 'libfoo.so' and 'crazy.libbar.so', with the following data:
// - first lib:  'This is the first test library!'
// - second lib: 'This is the second test library!'
const unsigned char lib_archive_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0x86,
    0xa3, 0x4c, 0x84, 0x92, 0x26, 0x8e, 0x1f, 0x00, 0x00, 0x00, 0x1f, 0x00,
    0x00, 0x00, 0x16, 0x00, 0x1c, 0x00, 0x6c, 0x69, 0x62, 0x2f, 0x74, 0x65,
    0x73, 0x74, 0x2d, 0x61, 0x62, 0x69, 0x2f, 0x6c, 0x69, 0x62, 0x66, 0x6f,
    0x6f, 0x2e, 0x73, 0x6f, 0x55, 0x54, 0x09, 0x00, 0x03, 0xb0, 0x22, 0xeb,
    0x5a, 0xb0, 0x22, 0xeb, 0x5a, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xab,
    0x6f, 0x00, 0x00, 0x04, 0x53, 0x5f, 0x01, 0x00, 0x54, 0x68, 0x69, 0x73,
    0x20, 0x69, 0x73, 0x20, 0x74, 0x68, 0x65, 0x20, 0x66, 0x69, 0x72, 0x73,
    0x74, 0x20, 0x74, 0x65, 0x73, 0x74, 0x20, 0x6c, 0x69, 0x62, 0x72, 0x61,
    0x72, 0x79, 0x21, 0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xd4, 0x86, 0xa3, 0x4c, 0xa0, 0xb1, 0x85, 0x09, 0x20, 0x00, 0x00,
    0x00, 0x20, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x1c, 0x00, 0x6c, 0x69, 0x62,
    0x2f, 0x74, 0x65, 0x73, 0x74, 0x2d, 0x61, 0x62, 0x69, 0x2f, 0x63, 0x72,
    0x61, 0x7a, 0x79, 0x2e, 0x6c, 0x69, 0x62, 0x62, 0x61, 0x72, 0x2e, 0x73,
    0x6f, 0x55, 0x54, 0x09, 0x00, 0x03, 0xb0, 0x22, 0xeb, 0x5a, 0xb0, 0x22,
    0xeb, 0x5a, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xab, 0x6f, 0x00, 0x00,
    0x04, 0x53, 0x5f, 0x01, 0x00, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73,
    0x20, 0x74, 0x68, 0x65, 0x20, 0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64, 0x20,
    0x74, 0x65, 0x73, 0x74, 0x20, 0x6c, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79,
    0x21, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03, 0x0a, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xd4, 0x86, 0xa3, 0x4c, 0x84, 0x92, 0x26, 0x8e, 0x1f, 0x00, 0x00,
    0x00, 0x1f, 0x00, 0x00, 0x00, 0x16, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x6c,
    0x69, 0x62, 0x2f, 0x74, 0x65, 0x73, 0x74, 0x2d, 0x61, 0x62, 0x69, 0x2f,
    0x6c, 0x69, 0x62, 0x66, 0x6f, 0x6f, 0x2e, 0x73, 0x6f, 0x55, 0x54, 0x05,
    0x00, 0x03, 0xb0, 0x22, 0xeb, 0x5a, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04,
    0xab, 0x6f, 0x00, 0x00, 0x04, 0x53, 0x5f, 0x01, 0x00, 0x50, 0x4b, 0x01,
    0x02, 0x1e, 0x03, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0x86, 0xa3,
    0x4c, 0xa0, 0xb1, 0x85, 0x09, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
    0x00, 0x1c, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa4, 0x81, 0x6f, 0x00, 0x00, 0x00, 0x6c, 0x69, 0x62, 0x2f, 0x74,
    0x65, 0x73, 0x74, 0x2d, 0x61, 0x62, 0x69, 0x2f, 0x63, 0x72, 0x61, 0x7a,
    0x79, 0x2e, 0x6c, 0x69, 0x62, 0x62, 0x61, 0x72, 0x2e, 0x73, 0x6f, 0x55,
    0x54, 0x05, 0x00, 0x03, 0xb0, 0x22, 0xeb, 0x5a, 0x75, 0x78, 0x0b, 0x00,
    0x01, 0x04, 0xab, 0x6f, 0x00, 0x00, 0x04, 0x53, 0x5f, 0x01, 0x00, 0x50,
    0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0xbe,
    0x00, 0x00, 0x00, 0xe5, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned int lib_archive_zip_len = 441;

}  // namespace testing
}  // namespace crazy