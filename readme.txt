/***********************************************************

PSP Audio Recorder Beta for 1.50/2.00	Ver.2006.10.07

***********************************************************/


moonlight氏が作成したPSP Audio Recorder Betaを
DEVHOOK向けのモジュールとしてコンパイルしなおしたものです。
http://forums.qj.net/f-psp-development-forum-11/t-psp-audio-recorder-beta-48162.html


ファームウェア1.50、2.00で動作します。
それ以降のファームウェア
2.50/2.60/2.71/2.80などでは動作しません。


●インストール
record.prxをpspbtcnf_game.txtに書き加えてください。
usbhostを使う場合はusbフォルダに有るものを使ってください。

例:
/kd/fatmsmod.prx
ms0:/dh/kd/devhook.prx
ms0:/dh/kd/mok/usbhostfs.prx
ms0:/dh/kd/umdciso.prx
ms0:/dh/kd/mok/screenshotbmp.prx
ms0:/dh/kd/record.prx	#追加
/kd/isofs.prx



●使い方
ゲーム中に下記のキーを押す事で録音ができます。
	L+R+○	録音スタート
	L+R+□	録音ストップ

録音されたファイルはms0:/PSP/MUSIC、又はusbhostfs0:/MUSICに保存されます。


●更新履歴
Ver.2006.10.06	・初版
Ver.2006.10.07	・バージョン別に分けていたのを1つのファイルに結合
		・usbhostが正しく動作していなかったのを修正

ねこかぶ
HP     :http://nekokabu.s7.xrea.com/
mail   :nekokabu@gmail.com
