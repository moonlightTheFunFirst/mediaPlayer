# Windows 開発環境

アプリ名（仮称）は **Orange**。タイトルバー・タスクバー・実行ファイルに葉付きオレンジのアイコンを使用する。
実行ファイル名は `Orange.exe`。
ログはQtのAppLocalDataLocation（通常 `%LOCALAPPDATA%/Keishin/Orange/player.log`）に保存する。
アイコンの原画像は `assets/orange.png`、Windows用の複数サイズICOは `windows/resources/orange.ico`。

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
生成先は `windows/build/msvc-release/Release/Orange.exe`。
起動中の対象プレイヤーがある場合はエラーで止まるので、閉じてから再実行する。

## 配布用フォルダの作成

```bat
windows\rebuild_and_deploy.bat
```

クリーンビルド後、`windeployqt`でQt・Multimediaプラグイン・FFmpeg DLL・VC++再頒布用インストーラーを収集する。
必要ファイルと実行ファイルのSHA-256を確認してから `windows/output/` を更新する。
以前のoutputは `windows/build/output-backup-…/` に保持する。
出力内の `Orange.exe` を実行する。別PCに移すときは **outputフォルダ全体** をコピーする。
VC++ランタイムがないPCでは、同梱の `vc_redist.x64.exe` を実行する。
バッチによるランタイムの自動インストールは行わない。

### 配布物の軽量化

VLCは再生・デコード・音声出力・描画用カテゴリを選んで同梱する。VLC専用GUI、配信・エンコード出力、機器探索、外部制御、ビジュアライザー等は含めない。
Qtの画像形式プラグインはアプリアイコン用のICOを残し、未使用の画像形式・SVGアイコンエンジン・TLS・ネットワーク情報プラグインを除外する。
VC++ランタイムのインストーラー、描画の代替DLL、日本語翻訳、ライセンス文書は保持する。
`README.md` と `WINDOWS.md` はoutputにコピーしない。開発・操作手順はリポジトリ内のREADMEを参照する。
Runモードでも管理対象の古い配布ファイルを取り除いてから配置するため、以前の広い構成が残らない。

Qt 6.8.3 / VLC 3.0.23で、454ファイル・230.5MiBから372ファイル・191.9MiBへ削減した（約38.6MiB減）。
開発用Qt/VLCパスを外した配布構成で21件のテストが成功。DVDの映像・音声出力、タイトル選択・シーク、MP4/WMV/AVI、サムネイル生成、アイコン、D&D、キーボード・全画面を確認。
既知の不安定なホバーテスト2件は今回のテスト対象から除外した。別PC・別DVDの追加検証は未実施。

別のQtキットを使う場合は、事前に `QT_MSVC_DIR` をMSVC x64版のルートに設定する。
既定は `F:\Qt\6.8.3\msvc2022_64`。MinGW版とは混在させない。
スクリプトのFFmpeg DLL確認はこのQt 6.8.3キットのバージョンを対象にしている。
自動実行では `MEDIAPLAYER_NO_PAUSE=1` を設定すると、失敗時のpauseを省略できる。

## 操作

### DVD-Video ISO（初期対応）

- `.iso` を「開く」・D&D・コマンドラインで指定できる。DVDはlibVLC 3.x、通常動画はQt Multimediaで再生する。
- 最長の通常タイトルを本編候補として選択する。別のタイトルは「再生 → DVDタイトル」から選択する。
- 再生・一時停止・停止・10秒送り／戻し・音量・全画面に対応。DVDの非正方形ピクセルを補正する。
- DVDメニュー操作、チャプター選択、音声／字幕トラック選択、Blu-ray・データISOは初期版の対象外。ISOのホバーは時刻のみで、サムネイルは表示しない。
- 暗号化されていないDVD-Videoを対象に検証する。すべてのディスク構造や暗号化されたISOの再生は保証しない。

ビルドにはVLC 3.x x64が必要。既定は `C:/Program Files/VideoLAN/VLC`。
別の配置なら `ORANGE_VLC_DIR` を指定する。バッチはDLL・プラグインを出力先の `vlc/` に同梱するので、利用先でのVLCインストールは不要。
実行時も同じ環境変数でライブラリの場所を上書きできる。VLCがなくても通常動画の再生は可能。

検証では `DVD_ISO_SAMPLE` にDVD-Video ISOの絶対パスを設定して統合テストを実行する。未設定時はDVDの実素材テストをスキップする。
素材はコピー・変更せず読み取る。検証用ISOをGitや配布フォルダには含めない。

2026-09-12の確認：指定されたDVD-Video ISO 1本で、映像・音声出力統計、16:9補正、タイトル選択、停止再開時のタイトル保持、シーク、D&D、キーボード、全画面、MP4への切り替えを確認。配布フォルダのみのDLL構成でもDVDテストが成功。
人による聴取・長時間のAV同期評価、別PCでの動作、他のDVD構造は未確認。
既存の通常動画ホバーテスト2件はこの実行環境では失敗し、変更前コミットでも再現。ほかの通常動画の再生・操作・サムネイル生成テストは成功。

### 通常動画と共通操作

- 「ファイル」メニューの「開く…」またはCtrl+OでWMV / MP4 / AVIを選択すると再生する。
- ファイルをドロップすると、そのファイルに切り替える。複数ドロップ時は先頭1件を使用する。
- 動画表示領域へのドロップにも対応する（通常表示・全画面・再生中の切り替えを自動テストで確認）。フォルダやWeb URLは受け付けない。
- ▶で再生、⏸で現在位置のまま一時停止する。Spaceは再生／一時停止の切り替え。
- ⏪／⏩または左右キーで10秒戻る／進む。先頭・末尾を超えず、再生中・一時停止中の状態を維持する。音量スライダーにフォーカスがある場合も左右キーはシークに使用する。
- スピーカーアイコンまたはMでミュートを切り替える。音量は隣のスライダーで調整し、ミュート解除時も元の音量を保持する。
- 「再生」メニューの「停止（先頭に戻す）」で、選択ファイルを維持して先頭に戻る。
- シークバーはクリック・ドラッグに対応し、離した位置へ移動する。一時停止状態は維持する。
- シークバーにマウスを重ねると時刻を即時表示し、約200ミリ秒後にその付近の映像をサムネイル表示する。ドラッグ中・全画面でも使用でき、マウスを離すと閉じる。
- サムネイルは通常再生とは別の無音デコーダーで取得する。1秒単位で最大100枚をメモリー内に保持し、ファイル切り替え時に破棄する。画像は最大200×112pxで縦横比を維持する。
- 音声のみのファイルや画像取得に失敗した場合は時刻のみを表示する。取得は最大5秒で打ち切る。指定時刻付近の画像であり、フレーム単位の厳密な一致は保証しない。
- 全画面は四隅の枠アイコンまたはF11で切り替え、Escで解除する。全画面中も操作バーを表示する。
- 実行ファイルに動画パスを渡すことも可能：`Orange.exe "C:\動画\サンプル.mp4"`
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

プレビューの検証では、Qt付属MP4・AVI・WMVと上記WMV9素材からの画像取得、途中時刻の画像変化、キャッシュ、通常表示・全画面のホバー／ドラッグ、左右端の表示位置、通常再生の位置と一時停止状態の維持、古いファイルの取得結果の破棄、音声のみの時刻表示を確認。
`MEDIAPLAYER_PREVIEW_SCREENSHOT`でプレビューポップアップのPNG保存先を指定できる。
長時間・高解像度動画の負荷、全コーデック、別PCでの動作は未検証。
