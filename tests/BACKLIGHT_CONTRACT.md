Backlight integration contract (main owner)

Append-only UI action/state fields in tab5_ui.h. SETTINGS sends backlight_timeout_set=true only for an explicitly edited option
and backlight_timeout_s in {0,300,1800,3600}. An unset marker preserves backend. Validate, persist to NVS, publish known=true
and the persisted timeout in each snapshot; report existing SETTINGS result only
after persistence succeeds. Missing key defaults to 0 (never). Failed persistence
must retain prior policy and snapshot so UI can retry. Brightness remains the saved
manual 10..100 value, including while panel output is zero.

Port integration implemented in tab5_native_port.c/.h (DMA/rotation unchanged).
Main calls bool tab5_native_port_backlight_configure(uint32_t timeout_s,
uint8_t manual_pct) after port start and after successful settings persistence.
Thread-safe mailbox; validates timeout and 10..100 brightness, false on invalid.
UI consumer owns policy, extends 32-bit ms through wrap (sample at least every
2^31 ms); BSP output is applied outside UI mutex and retried on failure.
Raw touch is filtered before UI, swallowing whole wake gesture. ROT batches
consume only first detent. Legacy PRESS/LONG wake and consume once.

Hardware callback uses bool tab5_native_port_post_button(bool down,uint32_t ms).
Only debounced raw edges; no duplicate semantic PRESS/LONG. FIFO false means
retry same edge/timestamp. DOWN wakes immediately; consumed DOWN swallows UP,
short and long. Awake short PRESS emitted on UP; LONG once at600ms during IDLE
or late UP. Holding button refreshes activity. Existing15s focus ring independent. On a consumed encoder wake, port calls
tab5_ui_encoder_wake(ms): retained focus becomes visible with no action. The
next event (or remaining detents of the same batch) operates normally; no second
focus-only consume. Touch wake does not show the encoder ring.
Hardware API coordinated with Confucius; main owns adapter wiring/persistence.

Record sync UI is included under SETTINGS: record_sync_set marks an explicit
edit, record_sync is the desired global device option (default false). Backend
mirrors PC set recsync through the same option and publishes record_sync_known /
record_sync. Unknown snapshots display no data; false is legitimate when known.
Unset action markers preserve backend values for legacy zero-initialized fixtures.
Pending waits only for fields actually sent; existing SETTINGS result reports
persistence success/failure. Start + stop synchronization is now explicitly authorized by Sergey; backend owns behavior. UI label states both.
Rows 0..10 retained (9 time, 10 panel); 11 timeout, 12 record sync.

Host verification: gcc -std=c11 -Wall -Wextra -Werror tests/test_backlight.c
tab5/main/tab5_backlight.c -o backlight-test.exe; run backlight-test.exe.
