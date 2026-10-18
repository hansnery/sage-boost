/* Skeletal instancing transaction. The caller owns every descriptor object
 * and keeps it alive until capture/batch/replay have finished. No native
 * game structures, callbacks, effect values or render queues are mutated. */
#ifndef PF_INSTSUBMIT_H
#define PF_INSTSUBMIT_H

#include "d3d9vt.h"

#define PF_SUBMIT_REGS 256

typedef struct {
    void *dev, *game_vs, *our_vs, *game_decl, *our_decl;
    void *instance_vb, *previous_vb;
    unsigned instance_stream, input_streams, instance_offset;
    unsigned previous_offset, previous_stride;
    unsigned instance_register, instance_registers;
    unsigned bone_register, bone_registers;
    unsigned dip[6];
} pf_submit_desc;

typedef struct {
    HRESULT error;                 /* First failed operation, or S_OK. */
    unsigned failed_operation;
    unsigned committed;            /* Units in a successful instanced DIP. */
    unsigned replayed;             /* Successful ordinary DIPs, in order. */
    unsigned coherent;             /* All touched device state restored. */
    unsigned complete;             /* Every requested unit was submitted. */
} pf_submit_result;

typedef struct {
    pf_submit_desc draw;
    unsigned bones[PF_SUBMIT_REGS * 4], saved[PF_SUBMIT_REGS * 4];
    unsigned valid, open, operation;
} pf_submit;

/* Operation IDs identify rejected API calls in diagnostics. Read-only
 * observation by tests uses the same compiled object as the plugin. */
enum {
    PF_SUBMIT_SAVE = 1, PF_SUBMIT_SHADER, PF_SUBMIT_DECL, PF_SUBMIT_STREAM,
    PF_SUBMIT_INPUT_FREQ, PF_SUBMIT_INSTANCE_FREQ, PF_SUBMIT_CONSTANTS,
    PF_SUBMIT_DRAW, PF_SUBMIT_RESTORE_INPUT_FREQ, PF_SUBMIT_RESTORE_INSTANCE_FREQ,
    PF_SUBMIT_RESTORE_STREAM, PF_SUBMIT_RESTORE_DECL, PF_SUBMIT_RESTORE_SHADER,
    PF_SUBMIT_RESTORE_CONSTANTS, PF_SUBMIT_CAPTURE_BONES,
    PF_SUBMIT_REPLAY_BONES, PF_SUBMIT_REPLAY_DRAW, PF_SUBMIT_RESTORE_BONES,
    PF_SUBMIT_SEED_NULL_STREAM, PF_SUBMIT_PENDING_BONES, PF_SUBMIT_PENDING_DRAW,
    PF_SUBMIT_PENDING_RESTORE
};

/* A refused capture makes no state writes and leaves valid false. */
HRESULT pf_submit_capture(pf_submit *s, const pf_submit_desc *draw);
/* An API error invalidates the capture, even after a fully recovered batch.
 * The caller must disable optimization or establish a new qualified group.
 * No success is inferred merely because the final state is coherent. */
pf_submit_result pf_submit_batch(pf_submit *s, unsigned *staged, unsigned count);
pf_submit_result pf_submit_pending(pf_submit *s, unsigned *staged, unsigned count);

#endif
