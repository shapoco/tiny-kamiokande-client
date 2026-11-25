#ifndef TAB5_CP_OTA_HPP
#define TAB5_CP_OTA_HPP

// シリアルの 'c6ota' コマンド (ESP32-C6 のファーム更新) を登録する。serialStart() より前に呼ぶ
#ifdef __cplusplus
extern "C" {
#endif
void cpOtaRegister(void);
#ifdef __cplusplus
}
#endif

#endif
