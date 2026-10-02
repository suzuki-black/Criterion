/* sys.h — 起動時のシステム設定(R800 切替・VBLANK フック)。 */
#ifndef SYS_H
#define SYS_H

#include "types.h"

/* R800 DRAM モードへ切替える。MSX_VER < 3(C-BIOS 等)では何もしない。 */
void sys_r800(void);

/* H.TIMI(VBLANK)に fn を繋ぐ。fn は BIOS ISR 内(割込み禁止・全レジスタ退避済み)で呼ばれる。 */
void sys_hook_vblank(void (*fn)(void));

/* ページ 1-2 を RAM へ複製して RAM から実行する(以後カートリッジ ROM は見えない)。成功で 1。 */
u8 sys_ram_exec(void);

#endif /* SYS_H */
