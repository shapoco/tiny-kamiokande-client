# ボードをリセットしてシリアル出力を指定秒数だけ標準出力に流す。使い方: python read_serial.py /dev/ttyACM1 120 (pyserial が要る。ESP-IDF の python 環境にある)
import serial, sys, time
port, secs = sys.argv[1], float(sys.argv[2])
s = serial.Serial(port, 115200, timeout=1)
# USB-Serial-JTAG のリセット (esptool の hard_reset と同じ RTS パルス)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
t0 = time.time()
while time.time() - t0 < secs:
    line = s.readline()
    if line:
        sys.stdout.write(line.decode('utf-8', 'replace')); sys.stdout.flush()
