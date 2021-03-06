# Document Transitions

This directory contains the script interface and implementation of the Document
Transition, and Shared Element Transition APIs.

Document Transition is a type of an animated transition that allows content to
animate to a new DOM state easily. For instance, modifying the DOM to change the
background color is a change that can easily be done without document
transitions. However, document transition also allows the new background state
to, for example, slide in from the left instead of simply atomically appearing
on top of the content.

For a detailed explanation, please see the
[explainer](https://github.com/vmpstr/shared-element-transitions/blob/main/README.md)

## Code Structure

A new method is exposed on window.document, called createTransition(). This is
the main interface to getting a new transition object from JavaScript. It is
specified in the `document_create_transition.idl` and is implemented in
corresponding `.cc` and `.h` files.

When called, `createTransition()` constructs a DocumentTransition object which
is specified in `document_transition.idl` and is implemented in corresponding
`.cc` and `.h` files.

The rest of the script interactions happen with this object.

## Pseudo Element Tree

During the transition, the browser creates a sparse post layout representation
including content from nodes in the old and new DOM. This representation is
rendered using the following steps :

- The browser executes the Document lifecycle phases (until paint) to generate
  the state required to render a DOM element as an image (bounding box size,
  transform mapping the box to viewport space and relative paint order between
  elements). This state is tracked for a subset of elements (called shared
  elements) which should be animated independently during a transition.

- A tree of pseudo elements is generated to render the shared elements using
  this state. The pseudo element tree is styled after a style recalc pass is
  executed on the author DOM during a Document lifecycle update.

``` text
html
|_ ::transition
   |_ ::transition-container(foo)
   |  |_ ::transition-old-content(foo)
   |  |_ ::transition-new-content(foo)
   |
   |_ ::transition-container(bar)
      |_ ::transition-old-content(bar)
      |_ ::transition-new-content(bar)
```

The ::transition pseudo element is the root of this pseudo element tree. This
provides a shared stacking context for painting pseudo elements corresponding to
a shared element.

Each shared element is rendered using the following new pseudo elements :

- ::transition-container generates a box which maps to the shared element's quad
in author DOM.
- ::transition-old-content is a replaced element displaying the shared element's
cached content from the old DOM.
- ::transition-new-content is a replaced element displaying the shared element's
live content from the new DOM.

Each shared element is tagged with a developer provided string which can be used
as a custom ident to uniquely identify and target the corresponding generated
pseudo elements in UA and developer stylesheets. This string is tracked on the
PseudoElement class.

## Additional Notes

Note that this project is in early stages of design and implementation. To
follow the design evolution, please see [our github
repo](https://github.com/vmpstr/shared-element-transitions/). Furthermore, this
README's Code Structure section will be updated as we make progress with our
implementation.
