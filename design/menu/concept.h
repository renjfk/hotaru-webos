/* design/menu/concept.h - what a concept gives the gallery runner.
 *
 * A concept builds its view into the root it is handed and reacts to directional keys; a main
 * menu concept also has an in-stream menu for when the stand-in video is running. Its script is
 * what the recorder plays to produce the gallery clips and screenshots. Concepts are grouped in
 * studies (main.c): the main menu, and the "Add a console" flow.
 */
#ifndef CHIAKI_WEBOS_CONCEPT_H
#define CHIAKI_WEBOS_CONCEPT_H

#include "kit.h"

typedef enum { ST_END, ST_KEY, ST_WAIT, ST_SHOT, ST_REC, ST_REC_END, ST_STREAM } step_kind_t;
typedef struct {
    step_kind_t kind;
    int arg;             /* key (nav_t), milliseconds, or stream on/off */
    const char *name;    /* shot / clip file name */
    const char *caption; /* shown under it in the gallery */
} step_t;

#define S_KEY(k)     { ST_KEY, (k), NULL, NULL }
#define S_WAIT(ms)   { ST_WAIT, (ms), NULL, NULL }
#define S_SHOT(n, c) { ST_SHOT, 0, (n), (c) }
#define S_REC(n, c)  { ST_REC, 0, (n), (c) }
#define S_REC_END    { ST_REC_END, 0, NULL, NULL }
#define S_STREAM(on) { ST_STREAM, (on), NULL, NULL }
#define S_END        { ST_END, 0, NULL, NULL }

typedef struct {
    const char *id; /* gallery folder: "01-home" */
    const char *name;
    const char *tagline;
    const char *notes; /* HTML paragraphs for the gallery page */
    void (*build)(lv_obj_t *root);
    void (*nav)(nav_t key);
    /* on: the stream is running, hide the home view (the video shows), menu closed;
     * off: back to the home view. NULL for concepts without a stream */
    void (*set_stream)(bool on);
    const step_t *script;
    const char *sheet[3]; /* its shots on the contact sheet; NULL: the study's */
} concept_t;

extern const concept_t concept_home, concept_orbit, concept_rail;
extern const concept_t concept_add_setup, concept_add_inline, concept_add_sheet, concept_add_dialogs;

#endif
