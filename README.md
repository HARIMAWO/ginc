# GINC (Ga! Is Never Called)

純粋な C (C11) と LLVM-C API、re2c、bison を用いて開発した、FreeBSD / Linux 環境上で動作する C言語ライクな JIT コンパイラ / インタープリタシステムです。

## 言語の由来・名前の由来

### 名前の由来

**GINC** は、**"Ga! Is Never Called"**（「ｶﾞｯ」は決して呼ばれない）を意味する再帰的頭字語（Recursive Acronym）です。

日本の古き良きインターネットコミュニティ（2ちゃんねる）には、Java の `NullPointerException`（通称「ぬるぽ」）というミスや的外れな発言に対し、即座に殴打音を表す**「ｶﾞｯ」**と返して制裁を加える有名な古典的スラングが存在します。

GINC コンパイラおよびランタイムにおいて、構文エラー、未定義シンボル、ゼロ除算、境界外アクセスなどの致命的例外が発生した際に吐かれるエラーメッセージ `Ga! (Parse Error)` や `Ga! (Codegen Error)` は、まさにこの**「ぬるぽに対する容赦ない『ｶﾞｯ』」**に由来しています。

> **「コードが正しく、ぬるぽ（バグ）を踏まない限り、『ｶﾞｯ』は決して呼ばれることはない（Ga! Is Never Called）」**

この再帰的頭字語には、そんな堅牢なプログラミングへの祈りとユーモアが込められています。

### 言語設計の背景と目的

「C言語の持つ素直な文法、静的型の安心感、およびネイティブな実行速度」を損なわずに、「スクリプト言語のような手軽さ、動的な文字列操作、安全な例外処理機構」を併せ持つ処理系を目指して誕生しました。

PHP や Python などの動的スクリプト言語に見られる仕様上の落とし穴（独自の演算子優先順位など）を排し、厳格な標準 C 互換の構文・演算子体系を基盤としながら、LLVM JIT による高速実行と柔軟なホスト組み込み（`libginc`、将来的には Apache モジュール `mod_ginc` 等）を可能にするシステム言語として設計されています。

## 主な機能

* **C言語互換の基本仕様**
  * 基本型 (`int`, `char`, `short`, `long`, `unsigned`, `float`, `double`, `bool` / `_Bool`, `void`, `FILE*`), `const`, `enum`, `typedef`
  * 制御構文 (`if`/`else`, `while`, `do-while`, `for`, `switch`/`case`/`default`, `break`, `continue`, `goto`, `return`)
  * 演算子（算術・比較・論理・ビット演算 `& | ^ ~ << >>`・複合代入 `+=` 等・`++`/`--`・三項演算子 `?:`・カンマ演算子 `,`・明示キャスト・`sizeof`）
  * 関数定義・プロトタイプ宣言・可変長引数関数 (`...`, `<stdarg.h>`)・`main()` エントリポイント自動呼び出し (`int main(void)`, `int main(int argc, char* argv[])`)
  * 1次元・2次元配列（サイズ省略 `int a[] = {...}` 対応）、境界チェック付きファットポインタ（`&`, `*`, ポインタ演算, `char*` カーソル）、関数ポインタ、ラムダ式 (`[](int x) { ... }`)
* **独自拡張機能**
  * 動的型 `var`、文字列型 `str`、連想配列 (`var dict[str];`)、正規表現マッチ (`=~`)
  * `class` / `struct`（`public:` / `private:` アクセス制御・メソッド定義・`.` / `->` アクセス）
  * 例外処理 (`try` / `catch` / `finally` / `throw`)、`print()`
* **内蔵 C プリプロセッサ & 標準ライブラリ動的ロード**
  * `#include`, `#define`（オブジェクト・関数・可変長 `__VA_ARGS__` マクロ）, `#undef`, `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`, `#pragma once`
  * `#pragma ginc_lib "..."` による共有ライブラリ（`.so`）の動的ロード (`dlopen`)
  * 標準ヘッダ群 (`stdio.h`, `stdlib.h`, `string.h`, `math.h`, `stdarg.h`, `stdint.h`, `stdbool.h`, `stddef.h`, `ctype.h`, `time.h`)
* **組み込み用コアライブラリ (`libginc`)**
  * `libginc.so` / `libginc.a` と [src/ginc.h](src/ginc.h) により、C アプリケーション等から `ginc_eval_string()` / `ginc_eval_file()` を呼び出して JIT 実行可能

## 設計方針：class 構文とオブジェクト指向について

GINC には `class` / `struct` 構文がありますが、C++ のような本格的なオブジェクト指向（多重継承、仮想関数テーブル / vtable、演算子オーバーロード、複雑なコンストラクタの初期化リスト等）は意図的に導入していません。

* **コンパイラとランタイムの軽量性維持**: C11 + LLVM-C API によるシンプルな JIT コンパイラとしての見通しの良さと、C 言語互換の手続き型コードを素直に書ける体験を優先しています。
* **糖衣構文としての位置づけ**: GINC における `class` / `struct` / `union` は、連想配列ベースのデータ構造に `this` バインド付きの関数と `public:` / `private:` アクセス制御をまとめた**軽量な糖衣構文**です。
  * **※ `union` に関する注意**: C ソースとの構文互換のため `union` キーワードを受理しますが、内部実装は `struct` と同じく各メンバが独立したハッシュスロットとして扱われます。C 言語のような**複数メンバ間でのメモリ領域の共有（型パンニング等）は行われない**点にご注意ください。
* **ライブラリによる型拡張**: 正規表現やソケット、DB 接続などの拡張型は、重いクラス階層ではなく `typedef void*`（不透明ハンドル）と C スタイル関数群（`.h` + 動的ロード `.so`）として後付けで拡張できる設計を採用しています。

## 必要環境

* C コンパイラ (`clang` 推奨 / `cc` / `gcc`)
* LLVM 開発環境 (`llvm-config` または `llvm-config-devel`)
* `re2c`
* `bison`
* `make` (GNU make / BSD make)

## ビルドとインストール

```sh
./configure
make
sudo make install
```

### `configure` オプション

| オプション | 説明 |
| :--- | :--- |
| `--prefix=DIR` | インストール先プレフィックスを指定（デフォルト: `/usr/local`） |
| `--local` | `~/.local/bin` および `~/.local/ginc/` 配下にインストール |
| `--local-host` | `~/.local/<hostname -s>/bin` および `~/.local/<hostname -s>/ginc/` にインストール（NFS 共有ホーム向け） |
| `--debug` | `-fsanitize=address` を有効にしてビルド |

## 使い方

```sh
# スクリプトの実行
./ginc tests/test_varargs_printf.ginc

# AST・LLVM IR ダンプ付きで実行（--dump / -d または環境変数 GINC_DUMP=1）
./ginc --dump tests/test_varargs_printf.ginc
GINC_DUMP=1 ./ginc tests/test_varargs_printf.ginc

# 標準入力からの実行
./ginc < script.ginc
```

### 環境変数・オプション

* `-I<dir>` / `GINC_INCLUDE_PATH`: ヘッダファイル探索パスの追加（コロン区切り）
* `-L<dir>` / `GINC_LIB_PATH`: 共有ライブラリ（`.so`）探索パスの追加（コロン区切り）

### `libginc` 組み込み例

```c
#include <stdio.h>
#include "ginc.h"

int main(void) {
    GincEngine* eng = ginc_create();
    int ret = ginc_eval_string(
        eng,
        "#include <stdio.h>\nint main(void) { printf(\"Hello GINC!\\n\"); return 0; }\n",
        "embed.ginc", 0, NULL
    );
    ginc_destroy(eng);
    return ret;
}
```

## ディレクトリ構成

```text
.
├── configure        # ビルド設定スクリプト
├── Makefile.in      # Makefile テンプレート
├── src/             # コンパイラ・JIT・プリプロセッサ・ランタイムソース
├── std/             # 標準ライブラリヘッダおよび libginc_std.so ソース
└── tests/           # テストスイート
```

## ライセンス

This project is licensed under the [Apache License 2.0](LICENSE).
