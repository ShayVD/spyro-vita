/* The readable renderers' working memory (stage 7d2, decisions D124;
 * render_hand.h SCRATCH_BASE): in place of the PS1 scratchpad. A call may
 * use any of it and nothing reads it after the call (D124's poison audit,
 * which went with the register-file builds it audited, D127), so it is a
 * buffer, not state. The presenter build has its own
 * (render_ctx_names.h: g_pres_scratch). */
unsigned char g_render_work[1024] __attribute__((aligned(1024)));
