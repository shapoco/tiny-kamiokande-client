#ifndef TK_NVS_STORE_HPP
#define TK_NVS_STORE_HPP

// NVS に置く設定 (WiFi の認証情報など)。名前空間 NVS_NAMESPACE のブロブを鍵で読み書きする。
// 最初の呼び出しで NVS を初期化し、壊れていれば消して作り直す

#include <cstddef>

namespace tkrunner {

// 鍵のブロブを data に読む。無いか大きさが違うか、NVS が使えなければ false
bool loadBlob(const char *key, void *data, size_t bytes);

// 書いてコミットする
bool saveBlob(const char *key, const void *data, size_t bytes);

}  // namespace tkrunner

#endif
