# Wizardry 7: Crusaders of the Dark Savant DOS/V版 DOSBox-X AutoMap Mod

## 概要

このリポジトリは、**Wizardry 7: Crusaders of the Dark Savant DOS/V版**をDOSBox-X上で遊ぶための、非公式AutoMap Modです。

このModを導入すると、ゲーム画面とは別にAutoMap用ウィンドウが表示され、ゲーム中の移動に合わせてマップが自動生成されます。

<img width="1878" height="1085" alt="image" src="https://github.com/user-attachments/assets/22e48d3a-47e5-4c1c-91c6-7f9f0bdda34b" />

本Modは、**日本語DOS/V版 Wizardry 7**向けです。  
**Wizardry 6や他のWizardry 7のAutoMap機能は含まれていません。**  

## できること

このModでは、主に以下のことができます。

- Wizardry 7 DOS/V版のプレイ中に、別ウィンドウでAutoMapを表示
- 移動に合わせたマップの自動生成
- 暗闇エリアでの表示制御(未テスト)
- マップウィンドウサイズの変更
- ツールチップ表示のON/OFF
- Windows 11上でDOSBox-Xを使った快適なプレイ環境を構築

## 含まれていないもの

このリポジトリおよびRelease ZIPには、以下のものは含まれていません。

- Wizardry 7のゲームデータ
- Wizardry 7本体に由来する著作物

ゲームを遊ぶには、利用者自身が正規に所有している **Wizardry 7: Crusaders of the Dark Savant DOS/V版** が必要です。

## 対応対象

対象は以下です。

```text
Wizardry 7: Crusaders of the Dark Savant DOS/V 日本語版
```

## 導入方法

### 1. まずWizardry 7 DOS/V版を遊べる状態にする

最初に、以下のリポジトリに従って、Windows 11上でWizardry 7 DOS/V版が起動できる状態にしてください。

**Windows 11でWizardry 7: Crusaders of the Dark Savant DOS/V版(256色)が遊べるキット**  
https://github.com/tamanegitaro/Wizardry_7_CDS_DOSV_Installer

この時点で、通常のDOSBox-X環境としてゲームが起動できる状態にしておきます。

### 2. DS.EXEをUnpack済みのものに置き換える

次に、以下のリポジトリに従い、`DS.EXE`をUnpackしたものに置き換えてください。

**Wizardry 7 DOS/V版 DS Unpacker**  
https://github.com/tamanegitaro/Wizardry_7_CDS_DOSV_DS_Unpacker

AutoMap機能は、Unpack済みの `DS.EXE` を前提としています。

### 3. AutoMap Modを導入する

このリポジトリのReleaseから、以下のファイルをダウンロードしてください。

```text
Wizardry7_CDS_DOSBox-X_Automap_Rev1.zip
```

ZIPを展開すると、以下のような構成になっています。

```text
Config\
  dosbox-x_wiz7.conf

DOSBox-X\
  dosbox-x.exe
```

既存のWizardry 7 DOS/V環境に対して、以下の2ファイルを上書きコピーしてください。

```text
Config\dosbox-x_wiz7.conf
DOSBox-X\dosbox-x.exe
```

その後、通常どおりゲームを起動します。

正常に導入できていれば、以下の2つのウィンドウが表示されます。

```text
1. Wizardry 7のゲーム画面
2. AutoMap用ウィンドウ
```

ゲーム内で移動すると、AutoMapウィンドウにマップが自動生成されます。

## AutoMap設定

`Config\dosbox-x_wiz7.conf` の `[automap]` セクションで、AutoMap機能を設定できます。

標準設定は以下です。

```ini
[automap]
enable=true
show_tooltips=true
hide_in_dark_zones=true
width=512
height=512
wiz7_sns_mode=false
```

### enable

```ini
enable=true
```

AutoMap機能を有効にするかどうかを指定します。

falseにするとAutoMapを無効にします。

通常は `true` のままで使用してください。

### show_tooltips

```ini
show_tooltips=true
```

AutoMapウィンドウ上でツールチップを表示するかどうかを指定します。

### hide_in_dark_zones

```ini
hide_in_dark_zones=true
```

Trueの場合、ゲーム内の暗闇エリアで、AutoMap表示を隠します。
快適性を優先する場合は `false` にしてください。

### width / height

```ini
width=512
height=512
```

AutoMapウィンドウのサイズを指定します。
標準では、512×512ピクセルです。

### wiz7_sns_mode

```ini
wiz7_sns_mode=false
```

Wizardry 7用の互換・実験用設定です。
機能はよくわかりませんが、よりゲーム内のマップ表示の仕様に近づくようです。

## ウィンドウ配置

標準の `Config\dosbox-x_wiz7.conf` では、ゲーム画面側の設定は以下のようになっています。

```ini
windowresolution  = 1280x1024
windowposition    =
```

`windowresolution` は、DOSBox-Xのゲーム画面ウィンドウサイズです。

`windowposition` は、DOSBox-Xのゲーム画面ウィンドウを表示する位置です。

```ini
windowposition = X,Y
```

の形式で指定します。

未指定の場合は、OS側の自動配置になります。

```ini
windowposition =
```

AutoMapウィンドウは標準で512×512です。  
ゲーム画面とAutoMapを横に並べる場合、以下のような配置が使いやすいです。

### 1920×1200ディスプレイの場合

1920×1200の画面では、標準の1280×1024ゲーム画面と512×512 AutoMapを横に並べやすいです。

おすすめ例です。

```ini
windowresolution  = 1280x1024
windowposition    = 600,32
```

## 注意事項

このModは非公式です。

本Modの使用によって発生した問題について、Wizardry 7の権利者、DOSBox-X開発者、AutoMap Modの原作者・refactor作者へ問い合わせないでください。

また、ゲーム本体の著作物は一切含めていません。  
利用者自身が正規に所有しているゲームデータを使用してください。

## Sourceフォルダについて

このリポジトリの `Source` フォルダには、以下を格納しています。

```text
DOSBox-X v2026.06.02 ベースのソースコード
Wizardry 7 DOS/V AutoMap対応の改造コード
```

Releaseに含まれる `dosbox-x.exe` は、この `Source` フォルダ内のソースコードに対応するバイナリです。

## License

This project is distributed under the **GNU General Public License version 2**.

This repository contains modified DOSBox-X source code and AutoMap-related code.

See `LICENSE` / `COPYING` for details.

## Acknowledgements

This project is based on the work of the **Wizardry 6 & 7 Automap Mod**.

I would like to express my deepest gratitude to the original author and the refactor author.  
Without their work, this DOS/V AutoMap adaptation would not have been possible.

Original: Copyright (C) 2014 KoriTama  
Wizardry 6 Automap Mod:  
https://www.moddb.com/mods/wizardry-6-automap-mod

Wizardry 7 Automap Mod:  
https://www.moddb.com/mods/wizardry-7-automap-mod

Refactor: Copyright (C) 2025 DungeonCrawl-Classics.com  
Wizardry 7 Map Details:  
https://dungeoncrawl-classics.com/wizardry-series/7-crusaders-of-the-dark-savant/wizardry-7-map-details/

This project is an unofficial adaptation for the Japanese DOS/V version of Wizardry 7.  

