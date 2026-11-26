// デモモードで流す応答 (impl/espcommon/samples/*.cbor) を Flash に埋め込む。
// パスは CMakeLists.txt が TK_SAMPLE_S1..3 で渡す
    .section .rodata.tk_samples, "a"
    .balign 4
    .global tk_s1_start, tk_s1_end, tk_s2_start, tk_s2_end, tk_s3_start, tk_s3_end
tk_s1_start:
    .incbin TK_SAMPLE_S1
tk_s1_end:
    .balign 4
tk_s2_start:
    .incbin TK_SAMPLE_S2
tk_s2_end:
    .balign 4
tk_s3_start:
    .incbin TK_SAMPLE_S3
tk_s3_end:
