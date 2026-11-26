# Tiny Kamiokande クライアント

[English](README.md) | 日本語

![](./image/cover.jpg)

Tiny Kamiokande の MCU 向けクライアント。
[Super-Kamiokande Realtime Monitor](https://www-sk.icrr.u-tokyo.ac.jp/realtimemonitor/) が公開しているイベントディスプレイ画像を
Tiny Kamiokande のサーバが解析したデータを取得し、検出器の 3D モデルに貼り付けて、サーバが推定した粒子の軌跡とともに表示します。

PC やスマートフォン向けの Web 版は https://www.shapoco.net/tiny-kamiokande/ で公開しています。

表示は学術的な正しさを保証しません (軌跡、方位ともに画像と公開情報からの推定です)。

## 対応機種

- M5Stack CoreS3 (ESP32-S3)
- M5Stack Tab5 (ESP32-P4 + ESP32-C6)
- Waveshare Pico-ResTouch-LCD-2.8 + Raspberry Pi Pico 2 W (RP2350)
- ホスト版 (PC。保存した応答からフレームを PNG に書き出す確認用)

## コンパイル済みバイナリ

コンパイル済みのファームは [Releases](https://github.com/shapoco/tiny-kamiokande-client/releases) からダウンロードできます。
zip を展開し、機種のディレクトリにあるファームを書き込みます。

M5Stack は `upload.sh` で書き込みます ([esptool](https://github.com/espressif/esptool) が必要です)。

```sh
m5cores3/upload.sh /dev/ttyACM0   # CoreS3
m5tab5/upload.sh /dev/ttyACM0     # Tab5
```

Pico-ResTouch-LCD-2.8 + Pico 2 W は、ツールなしで書き込めます。

1. Pico 2 W の BOOTSEL ボタンを押しながら、USB ケーブルで PC に接続する (接続したらボタンを離してよい)。
2. `RP2350` という名前のドライブが現れるので、`pico_restouch_lcd_2_8/tiny_kamiokande_pico_restouch_lcd_2_8.uf2` をそこにコピーする。
3. コピーが終わると自動で再起動して起動する (ドライブは消える)。

Tab5 は書き込んだ後に C6 の更新が必要です (下記)。

## ソースからのビルドに必要なもの

- [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 (環境変数 `IDF_ROOT`、既定 `~/esp/5.5/esp-idf`)
- [ShapoGFX](https://github.com/shapoco/shapo-gfx) のクローン (環境変数 `SHAPOGFX_PATH`、既定 `~/shapo-gfx`)
- Pico 版は [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.2 (環境変数 `PICO_SDK_PATH`、既定 `~/pico/pico-sdk`)、Arm GNU Toolchain、picotool

## ビルドと書き込み

```sh
impl/m5cores3/run.sh /dev/ttyACM0   # CoreS3 (ポートは環境に合わせる)
impl/m5tab5/run.sh /dev/ttyACM0     # Tab5
impl/m5cores3/build.sh -DTKAPP_DEMO=1   # 通信せず埋め込みのサンプルを表示するデモモード
impl/pico_restouch_lcd_2_8/build.sh     # Pico-ResTouch-LCD-2.8 + Pico 2 W (-DTKAPP_DEMO=ON でデモモード)
impl/pico_restouch_lcd_2_8/flash.sh     # picotool で書き込む
```

Tab5 は出荷時の ESP32-C6 のファームのままでは HTTPS が通らないため、最初に C6 の更新が必要です。
Tab5 のファームには C6 の更新用ファームが埋め込まれており、USB シリアルのコンソールで `c6ota` コマンドを実行すると更新できます (約 11 秒。[SPEC.md](SPEC.md) 5.6)。

## WiFi の設定

画面右上の WiFi アイコンをタップすると設定画面が開きます。USB シリアルのコンソールで `wifi` コマンドを使って設定することもできます。
設定は本体の NVS (Pico は Flash の最後のセクタ) に保存されます。

画面下部の記録のすぐ上の帯は直近 1 時間の時間軸で、検出器内で停止したイベント (黄) と検出器内で発生したイベント (赤) だけを点で示します。点をタップするとそのイベントを再生し、▶ LIVE で最新の表示に戻ります。

Pico-ResTouch-LCD-2.8 はシングルタッチなので、ピンチの代わりに検出器のダブルタップで拡大、長押しで縮小します。

## ドキュメント

仕様、サーバ API、実装の詳細は [SPEC.md](SPEC.md) を参照してください。
