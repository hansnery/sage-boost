/* Offline only. The caller owns the device/effect and invalidates this
 * mirror after any unobserved device mutation. Never linked into the ASI. */
void bench_filter_init(IDirect3DDevice9 *, const unsigned char *,const unsigned char *);
ID3DXEffectStateManager *bench_filter_manager(void);
void bench_filter_mode(unsigned); /* 1=forward, 2=filter within one effect group */
void bench_filter_invalidate(void);
HRESULT bench_filter_begin(void);
HRESULT bench_filter_end(void);
void bench_filter_counts(unsigned long long *, unsigned long long *);
void bench_filter_clear(void);
