#ifndef WINXPLAY_INPUT_MESSAGES_H
#define WINXPLAY_INPUT_MESSAGES_H
// Stable, pointer-free window messages for an OEM touch/key/CAN adapter.
// Post to the WinXPlay top-level window. Standard WM_MOUSE/WM_KEY work as well.
enum {
    // WP: slot (0/1) in low byte, down (0/1) in bit 8.
    // LP: normalized X/Y as unsigned 16-bit values, 0..65535.
    WINXPLAY_INPUT_CONTACT=WM_APP+20,
    // WP: cp::InputKey; LP: down (0/1).
    WINXPLAY_INPUT_KEY=WM_APP+21,
    // WP: signed wheel steps; LP: signed 16-bit X/Y controller axes.
    WINXPLAY_INPUT_NAVIGATE=WM_APP+22,
    // WP: night (0/1).
    WINXPLAY_INPUT_NIGHT=WM_APP+23,
    WINXPLAY_INPUT_CANCEL=WM_APP+24,
    WINXPLAY_INPUT_KEYFRAME=WM_APP+25,
    // WP: visible (0/1); request/show the receiver from the vehicle UI.
    WINXPLAY_INPUT_VISIBLE=WM_APP+26
};
#endif
