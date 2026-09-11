# Windows 開発環境

`F:\keishin\programs\splitFiler` の構成を参考に、Windows専用のスクリプト・環境確認・ビルド出力をこのディレクトリにまとめる。
参照元の `win/` に相当する名前として、このプロジェクトでは `windows/` を使用する。
共通のアプリケーションコードはルートの `src/`、共通ビルド定義はルートの `CMakeLists.txt` に置く。
Windows固有のビルド・配布処理はこのディレクトリ内で管理する。

## ビルド・起動

エクスプローラーから `rebuild_and_run.bat` を実行するか、リポジトリのルートで次を実行する。

```bat
windows\rebuild_and_run.bat
```

CMakeでMSVC x64 Releaseを構成し、クリーンビルド、Qt DLL配置後にプレイヤーを起動する。
生成先は `windows/build/msvc-release/Release/mediaPlayer.exe`。
起動中の対象プレイヤーがある場合はエラーで止まるので、閉じてから再実行する。

## 配布用フォルダの作成

```bat
windows\rebuild_and_deploy.bat
```

クリーンビルド後、`windeployqt`でQt・Multimediaプラグイン・FFmpeg DLL・VC++再頒布用インストーラーを収集する。
必要ファイルと実行ファイルのSHA-256を確認してから `windows/output/` を更新する。
以前のoutputは `windows/build/output-backup-…/` に保持する。
出力内の `mediaPlayer.exe` を実行する。別PCに移すときは **outputフォルダ全体** をコピーする。
VC++ランタイムがないPCでは、同梱の `vc_redist.x64.exe` を実行する。
バッチによるランタイムの自動インストールは行わない。

別のQtキットを使う場合は、事前に `QT_MSVC_DIR` をMSVC x64版のルートに設定する。
既定は `F:\Qt\6.8.3\msvc2022_64`。MinGW版とは混在させない。
スクリプトのFFmpeg DLL確認はこのQt 6.8.3キットのバージョンを対象にしている。
自動実行では `MEDIAPLAYER_NO_PAUSE=1` を設定すると、失敗時のpauseを省略できる。

## 操作

- 「開く」またはCtrl+OでWMV / MP4 / AVIを選択すると再生する。
- ファイルをドロップすると、そのファイルに切り替える。複数ドロップ時は先頭1件を使用する。
- 動画表示領域へのドロップにも対応する（通常表示・全画面・再生中の切り替えを自動テストで確認）。フォルダやWeb URLは受け付けない。
- 再生／一時停止はボタンまたはSpace、ミュートはボタンまたはM。
- 停止すると選択ファイルを維持して先頭に戻る。
- シークバーはクリック・ドラッグに対応し、離した位置へ移動する。一時停止状態は維持する。
- 全画面はF11、解除はEsc。全画面中も操作バーを表示する。
- 実行ファイルに動画パスを渡すことも可能：`mediaPlayer.exe "C:\動画\サンプル.mp4"`
- Qtの診断ログは `QStandardPaths::AppLocalDataLocation` の `player.log` に記録する。

対応表と未実装機能はルートの [README.md](../README.md) を参照。
依存ライブラリと同梱ライセンスは [THIRD_PARTY.md](THIRD_PARTY.md) を参照。

## 構成

- `scripts/`：Windows用の開発スクリプト
- `environment-check/`：Qtのコンパイル・リンク・初期化確認用の最小プログラム
- `build/`：生成物（Git管理対象外）
- `output/`：今後の配布出力先（Git管理対象外）

## 確認結果（2026-09-11）

| 項目 | 確認内容 |
| --- | --- |
| Qt（今回使用） | 6.8.3、`F:\Qt\6.8.3\msvc2022_64` |
| Qtモジュール | Core / Gui / Widgets / Multimedia / MultimediaWidgets のコンパイル・リンク成功 |
| コンパイラ | Visual Studio Build Tools 2022 17.14.12、MSVC 19.44.35214.0、x64 |
| Windows SDK | CMakeが10.0.26100.0を選択 |
| CMake | 4.1.0、`C:\Program Files\CMake\bin\cmake.exe` |
| Ninja | 1.13.1、`C:\tools\ninja.exe`（今回のビルドでは未使用） |
| その他のQtキット | 6.8.2 / 6.8.3のMinGW版が存在（ビルド未検証） |
| MinGW | `F:\Qt\Tools\mingw1310_64`が存在（実行未検証） |
| 再生プラグイン | QtのmultimediaフォルダにFFmpeg / Windows Mediaプラグインが存在 |
| FFmpeg DLL | Qtのbinフォルダにavcodec-61 / avformat-61 / avutil-59が存在 |
| 最小プログラム | Debugビルド成功。QApplication / QAudioOutput / QVideoWidget / QMediaPlayerを生成・接続し、終了コード0で終了 |

Qt Multimediaを第一候補として進める。プラグインやDLLの存在、初期化成功は、各メディア形式の再生対応を保証しない。
WMV9/WMA、MP4/H.264/AAC、AVI/MPEG-4 Part 2などの検証結果はルートREADMEの対応表を参照。
別PCでの起動、聴取による音声確認、AV同期評価は未確認。

## 環境確認の実行

リポジトリのルートでPowerShellから実行する。CMakeがPATHにあり、Visual Studio 2022のC++ツールがインストールされていること。

```powershell
.\windows\scripts\check-environment.ps1
```

別のQt MSVC x64キットを使用する場合：

```powershell
.\windows\scripts\check-environment.ps1 -QtRoot 'F:\Qt\6.8.3\msvc2022_64'
```

生成先は `windows/build/environment-check-msvc/`。確認プログラムはウィンドウを表示せず終了する。
Qtのパス設定は確認プログラムを動かすプロセス内のみで変更し、実行後に復元する。

今回、Codexのサンドボックス内では環境変数 `Path` / `PATH` の重複によりMSBuildがMSB6001で失敗した。
通常環境で同じスクリプトを実行するとビルド・初期化が成功した。
CMakeのVulkanヘッダー未検出メッセージは出たが、今回の確認ターゲットのビルドは成功した。

## 統合テスト

Qtソースのテスト素材を使用する（配布物には含めない）。

```powershell
cmake -S . -B windows/build/tests-msvc -G 'Visual Studio 17 2022' -A x64 -DCMAKE_PREFIX_PATH=F:/Qt/6.8.3/msvc2022_64 -DMEDIAPLAYER_BUILD_TESTS=ON
cmake --build windows/build/tests-msvc --config Release --target playbackTests
$env:PATH = 'F:\Qt\6.8.3\msvc2022_64\bin;' + $env:PATH
$env:QT_PLUGIN_PATH = 'F:\Qt\6.8.3\msvc2022_64\plugins'
$env:QT_MEDIA_TEST_ROOT = 'F:\Qt\6.8.3\Src\qtmultimedia\tests\auto\integration'
.\windows\build\tests-msvc\Release\playbackTests.exe
```

`WMV9_SAMPLE`に音声付きWMV9ファイルの絶対パスを設定すると、その素材も検証する。
今回使用したファイルは `https://samples.ffmpeg.org/V-codecs/WMV9/nokia_n90.wmv`。
`WMV_AUDIO_SAMPLE`は追加のWMV素材向けの診断用設定。`welcome3.wmv`は現在、映像取得のテストが失敗する既知の制限がある。
`MEDIAPLAYER_SCREENSHOT`でテスト画面のPNG保存先を指定できる（GPU描画領域が画像に含まれない場合がある）。
