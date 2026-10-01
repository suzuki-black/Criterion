;; ============================================================================
;;  crt0rom.s — Criterion の ROM 起動コード("AB"ヘッダ@0x4000)＋ ASCII8 マッパー初期化。
;;  出典: ZeroNoHoukouR/src/crt0rom.s(実機確定)。バンクコール(_bcall)は M1 で必要になったら戻す。
;;
;;  32K ROM は INIT 時にページ1しか自スロットへ切替わらないので、page2 を ENASLT で
;;  有効化してから gsinit/_main を呼ぶ。gsinit は _DATA をゼロクリアしてから初期化子をコピー
;;  (ソフトリセット時の残留 state 対策)。
;; ============================================================================
        .module crt0rom
        .globl  _main
        .globl  l__INITIALIZER
        .globl  s__INITIALIZER
        .globl  s__INITIALIZED
        .globl  s__DATA
        .globl  l__DATA

        .area   _HEADER (ABS)
        .org    0x4000
        .db     0x41, 0x42      ; "AB"
        .dw     init            ; INIT エントリ
        .dw     0x0000          ; STATEMENT
        .dw     0x0000          ; DEVICE
        .dw     0x0000          ; TEXT
        .dw     0x0000          ; reserved
        .dw     0x0000
        .dw     0x0000

        .area   _CODE
init:
        ;; --- page2(0x8000-0xBFFF) を自カートリッジのスロットへ有効化 (32K ROM 必須) ---
        call    0x0138          ; RSLREG: A = 基本スロットレジスタ(ポート0xA8)
        rrca                    ; page1 のスロット番号(bit3-2)を bit1-0 へ
        rrca
        and     #0x03
        ld      c, a
        ld      b, #0
        ld      hl, #0xFCC1     ; EXPTBL
        add     hl, bc
        ld      c, a            ; C = 基本スロット番号
        ld      a, (hl)         ; 拡張スロットか?
        and     #0x80
        or      c               ; 拡張なら最上位ビットを立てる
        ld      c, a            ; C = スロットID(F00000PP)
        inc     hl
        inc     hl
        inc     hl
        inc     hl              ; HL -> SLTTBL[slot]
        ld      a, (hl)         ; 2次スロットレジスタ値
        and     #0x0C           ; page1/2 の2次スロットビット(bit3-2)
        or      c               ; F000SSPP を組み立て
        ld      h, #0x80        ; page2 指定
        call    0x0024          ; ENASLT (page2 を有効化)

        ;; --- ASCII8 マッパー初期化: 4窓を確定(bank0-2=コード固定, bank3=スワップ窓既定) ---
        ;;    書込アドレスでバンク選択。gsinit/main 呼出前に page2 側の窓を確定させる。
        ld      a, #0
        ld      (0x6000), a     ; 0x4000-0x5FFF = bank0
        ld      a, #1
        ld      (0x6800), a     ; 0x6000-0x7FFF = bank1
        ld      a, #2
        ld      (0x7000), a     ; 0x8000-0x9FFF = bank2
        ld      a, #3
        ld      (0x7800), a     ; 0xA000-0xBFFF = bank3 (スワップ窓の既定)

        ;; --- 以降 page2 のコード(gsinit/main)を安全に呼べる ---
        call    gsinit
        call    _main
loop:
        jr      loop            ; ROM: 戻らずループ

        .area   _INITIALIZER
        .area   _HOME
        .area   _GSINIT
gsinit:
        ;; --- 未初期化static(_DATA領域)をゼロクリア ---
        ;;   ★従来 gsinit は _INITIALIZER のコピーのみで _DATA(未初期化static)をゼロ化していなかった。
        ;;     初回起動はRAMが0の環境で「たまたま」動いていたが、ソフトリセットではRAMが残るため
        ;;     g_scene/dmode/phase/g_vmode 等の残留stateが残り、リセット後にゲームが誤動作していた
        ;;     (例: リセット→コナミ→config→開始でステージが始まらずタイトルへ戻る／表示崩れ)。
        ;;     ここで毎起動(リセット含む)に _DATA を0で埋め、冷起動と同じ清浄な初期状態にする。
        ;;   ※初期化子付きstatic(_INITIALIZED)は下の ldir で正しい値へ上書きされるので順序OK。
        ld      bc, #l__DATA
        ld      a, b
        or      a, c
        jr      Z, gs_copy
        ld      hl, #s__DATA
        ld      (hl), #0x00
        dec     bc
        ld      a, b
        or      a, c
        jr      Z, gs_copy
        ld      d, h
        ld      e, l
        inc     de
        ldir                    ; [s__DATA]=0 を全域へ伝播
gs_copy:
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      Z, gsinit_done
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir
gsinit_done:
        .area   _GSFINAL
        ret

        .area   _DATA
        .area   _INITIALIZED
        .area   _BSS
        .area   _HEAP
