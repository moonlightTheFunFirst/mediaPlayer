# 同梱ライブラリ

この開発用配布フォルダは Qt 6.8.3 の MSVC x64 キットから、Qt DLL・プラグイン、FFmpeg DLL、MSVCランタイムを収集します。
外部コーデックパックのインストールは前提にしません。

- Qt: https://www.qt.io/ および https://doc.qt.io/qt-6/licensing.html
- FFmpeg: https://ffmpeg.org/ および https://ffmpeg.org/legal.html
- MSVCランタイム: Microsoft Visual C++ Redistributable。windeployqtが収集した再頒布可能ファイルを使用します。

Qt / FFmpegは動的リンクで使用しています。実際のライセンス条件は使用したQtキットと各同梱コンポーネントに従います。
`licenses/Qt/`にQt 6.8.3ソースのqtbase/LICENSES、`licenses/FFmpeg/`にqtmultimedia/src/3rdparty/ffmpegのライセンス本文と帰属情報を同梱します。
Qt側の帰属情報はFFmpeg n7.1を記載しており、実行時のQtログでも7.1を確認しました。
このフォルダは現時点で開発・動作確認用です。外部配布時のソース提供方法と、同梱プラグインを含む第三者通知の最終確認は今後の配布整備で行います。
# DVD ISO playback: libVLC

DVD-Video ISO再生にVideoLANのlibVLC 3.xを動的に使用する。検証バージョンはWindows x64の3.0.23。
配布バッチは指定VLCフォルダから `libvlc.dll`、`libvlccore.dll`、`plugins/`、`COPYING.txt` を `output/vlc/` へコピーする。
libVLC APIはLGPL、VLCの各プラグイン・依存物にはGPL等の条件がある。DLLの動的読み込みだけで全同梱物がLGPLになるわけではない。
外部配布時には同梱バージョンに対応するライセンス・著作権表示・対応ソース提供等の条件を確認する。現時点ではローカル開発・動作確認用の配布フォルダを生成する。

- API: https://videolan.videolan.me/vlc-3.0/
- Source and build materials: https://download.videolan.org/pub/videolan/vlc/3.0.23/
- Project: https://www.videolan.org/vlc/libvlc.html
