# Editor-only elements ("Hide in Output")

This fork lets any element -- stroke, text, image, TeX formula, link -- be marked *editor-only*:
visible while editing, absent from everything meant for an audience. The motivating case is a
complicated construction, such as the scaffolding of a mathematical graph, that the presenter
wants to see while drawing over it on a recording, without the audience ever seeing it.

## What it does

An editor-only element is:

- **Shown faded on the editing canvas** (at 40% opacity), so it is clearly present and clearly
  special. It remains fully selectable, movable, erasable, and editable.
- **Left out of the video recording and the projector window.** Both draw their frames from the
  same renderer, so what the projector shows is what the recording contains: neither includes
  editor-only elements, even while they are selected.
- **Left out of PDF, PNG, and SVG exports, out of printing, and out of the file preview
  thumbnail** embedded in the saved document.

Sidebar page previews, being part of the editor, show editor-only elements faded like the canvas
does.

## Toggling

The **Hide in Output** action toggles the flag on the current selection:

- Toolbar: the crossed-out-eye button (`EDITOR_ONLY`; in the default layouts it sits next to the
  projector button). Available under *Selection Tools* when customizing the toolbar.
- Menu: **Edit → Hide Selection in Output**.

Select any mix of elements and trigger the action once: if anything in the selection is still
visible in output, everything becomes editor-only; a selection that is already entirely
editor-only is brought back. The change is one undo step. Without a selection the action does
nothing.

## File format

The flag is saved as a fork-only attribute on the element:

```xml
<stroke tool="pen" ... editorOnly="true">...</stroke>
```

As with the fork's other format extensions (line shapes, styled text runs), a document containing
any editor-only element is tagged with the fork's file format version, so a stock Xournal++ build
warns before opening it -- and then shows the elements normally, since it ignores attributes it
does not know. A document without any editor-only elements is written exactly as stock
Xournal++ would write it. Exports to the legacy `.xoj` format drop the attribute.

Copying and pasting preserves the flag inside the fork. Pasting an editor-only element into a
stock build alongside the fork does not work, matching the fork's other clipboard extensions.

## Known edge

While a text element is being edited in place, the live text edition overlay draws it on the
recorded frame even if the element is editor-only; it disappears from the frames again the moment
editing ends. Editing hidden scaffolding mid-recording is the only way to notice this.
