# シリアルコンソールの 'wifi' コマンドに認証情報を流し込み、その後の出力を指定秒数表示する。
# 使い方: python wifi_setup.py /dev/ttyACM1 tmp.wifi.md 90
#   wifi.md は "- ssid: `...`" と "- pass: `...`" の行を含むメモ (git 管理外の tmp.* に置く)。DHCP で接続する
import re, serial, sys, time

port, md, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
ssid = pw = None
for line in open(md, encoding='utf-8'):
    m = re.search(r'`([^`]*)`', line)
    if not m: continue
    if 'ssid' in line.lower(): ssid = m.group(1)
    elif 'pass' in line.lower(): pw = m.group(1)
assert ssid and pw, 'ssid/pass not found'
s = serial.Serial(port, 115200, timeout=0.2)
t0 = time.time()
def pump(until):
    while time.time() < until:
        line = s.readline()
        if line:
            out = line.decode('utf-8', 'replace').rstrip()
            if pw in out: out = out.replace(pw, '********')
            if ' fps,' not in out: print('%6.1f %s' % (time.time() - t0, out), flush=True)
def send(text, wait=1.0):
    s.write((text + '\r').encode()); s.flush(); pump(time.time() + wait)
# リセットして起動を待つ (USB-Serial/JTAG の RTS パルス)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
pump(time.time() + 6)
send('')          # プロンプトを出す
send('wifi')
send(ssid); send(pw); send('')   # DHCP
pump(t0 + secs)
