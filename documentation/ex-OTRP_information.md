**※extended版OTRPのドキュメントです。[standard版はこちら](https://github.com/teamhimeh/simutrans/blob/OTRP-distribute/documentation/OTRP_v13_information.md)**

# ex-OTRPとは？
jamespettsさんによるsimutrans extendedに機能をいくらか付け加えた改造版Simutransです。  
OTRPはOneway Twoway Road Patchの略で、日本語通称は「二車線パッチ」です。  
現行Simutransでは片側二車線の高速・幹線道路を引いても実質一車線分しか使ってくれないのでフルで2車線使ってくれるようにするのが本プロジェクトの目的です。

本家フォーラム: https://forum.simutrans.com/index.php/topic,18073.msg171856.html#msg171856

# 導入方法
1. pakフォルダの中にあるmenuconfでsimpletoolsの37番に適当なキーを割り当てる。例えばmenuconf.tabに`simple_tool[37]=,:`と追記すればコロンを押すとRibiArrowが出現する。
2. 本体をDLし、simutrans-extended.exeが入ってる所と同じディレクトリに実行ファイルを入れる。
3. DLした実行ファイルを起動する。元のsveファイルを上書きしないように気をつけましょう。
※Extended版ではribi_arrow.pakは必要ありません

# 使い方
[standard版OTRPのドキュメント](https://github.com/teamhimeh/simutrans/blob/OTRP-distribute/documentation/OTRP_v13_information.md)を参照してください。  
道路信号の進入方向制御は方向の表示がstandardとは逆になっています。使ってみればわかると思います。

# データの互換性
## アドオンの互換性
ex-OTRPはsimutrans extended向けのアドオンであれば全て使えます。OTRP専用アドオンというのは存在しません。
## セーブデータの互換性
- simutrans extended revision b9030aa 以前のセーブデータであればそのままex-OTRPで読み込めます。（2018年4月3日以前リリースのバージョンなら読み込めます。あまり古いデータはダメかもしれませんが。）
- standard版OTRPとの互換性はありません。standardのデータを読むことも（たぶん）できません。
- 一度セーブデータを読み込んでそれを**保存した瞬間に**そのデータは**ex-OTRP専用**になります。既存のデータをOTRPに移行する場合はバックアップを取った上で別ファイルとして保存することを強く推奨します。

# おねがい
バグ探しには皆さんのお力が必要です。バグと思われる挙動があれば@himeshi_hobに報告していただけるとありがたいです。  
特に「ネットワークプレイ」が安定動作するかが確認取れてないので遊んでみて動作状況を教えていただけるとうれしいです。ぜひOTRPでNSを楽しんでみてください。
