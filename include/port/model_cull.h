#ifndef PORT_MODEL_CULL_H
#define PORT_MODEL_CULL_H

/* Widescreen: the game parks actors just off its 320-pixel screen, where the
 * PS1 never showed them. A model whose every linked primitive lies left of
 * x = 0 or right of x = 320 is unlinked again when the wider view is shown.
 * Models that reach into the original screen are drawn whole. */
/* Whether the frame being drawn is presented wider than 4:3 (background coverage). */
void peModelCullSetWide(int enabled);
void peModelCullBegin(void);
/* After addPrim(head, packet) of one of the model's polygons. */
void peModelCullLinked(void* head, void* packet);
void peModelCullEnd(void);

#endif
