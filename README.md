# hexreclaim

维护一局 hex_reclaim 对局状态的 C99 库。只依赖标准库，不分配动态内存，没有可变全局状态，不做 I/O。

## 构建

```sh
make            # build/libhexreclaim.a、示例程序、测试程序
make test       # 运行测试
./build/play 2 3   # 命令行对局：半径 2，每方 3 子
```

也可以直接把 `include/hexreclaim.h` 和 `src/hexreclaim.c` 放进自己的工程。源文件是 UTF-8（含中文字符串），用 MSVC 时请加 `/utf-8`。

## 用法

```c
#include <stdio.h>
#include "hexreclaim.h"

int main(void)
{
    hr_game g;
    if (hr_init(&g, 2, 3) != HR_OK)        /* 半径 n=2，每方 m=3 子 */
        return 1;

    hr_apply(&g, hr_place(-2, 0));         /* P0 落子 */
    hr_apply(&g, hr_place(1, 0));          /* P1 落子 */

    hr_status s = hr_apply(&g, hr_sweep(-2, 0, HR_E));  /* P0 向东扫略 */
    if (s != HR_OK)
        printf("%s\n", hr_status_str(s));

    printf("P0 已占 %d 格\n", hr_claimed(&g, HR_P0));   /* 3 */
    hr_undo(&g);                            /* 撤销扫略 */
    return 0;
}
```

## 坐标与方向

轴坐标 `(q, r)`，中心为 `(0, 0)`；棋盘内的格子满足 `|q|, |r|, |q+r| <= n`。

```
        NW (0,-1)    NE (+1,-1)
     W (-1,0)    (q,r)    E (+1,0)
        SW (-1,+1)   SE (0,+1)
```

遍历全盘：

```c
int n = hr_radius(&g);
for (int r = -n; r <= n; r++)
    for (int q = -n; q <= n; q++) {
        hr_coord c = {q, r};
        if (hr_on_board(&g, c)) {
            hr_cell cell = hr_cell_at(&g, c);
            /* ... */
        }
    }
```

## 接口一览

| 用途 | 函数 |
|---|---|
| 开局 | `hr_init` |
| 棋盘 | `hr_radius` `hr_cell_count` `hr_on_board` `hr_cell_at` `hr_cell_owner` `hr_empty_count` |
| 局面 | `hr_to_move` `hr_is_over` `hr_winner` `hr_pieces_per_player` |
| 双方数据 | `hr_in_hand` `hr_pieces_on_board` `hr_claimed` `hr_score` |
| 操作 | `hr_place` / `hr_sweep`（构造）、`hr_check` `hr_apply` `hr_undo` |
| 分析 | `hr_legal_moves` `hr_ray_length` `hr_step` |
| 记录 | `hr_ply` `hr_history_move` |
| 错误 | `hr_status_str` |

详细约定见 `include/hexreclaim.h` 中的注释。

## 实现约定

规则以设计文档（2026-11-26 版）为准。下面是库层面的选择：

- `n`、`m` 尚未确定，因此都是运行时参数：`1 <= n <= 8`，`1 <= m <= 255`。
- `HR_P0` 固定先行。谁当 P0 由调用方决定。
- 对局结束时，压着棋子的格子仍显示为 `HR_PIECE_*`，计分时算给棋子主人。
- `hr_score` = 已占领格数 + 盘上棋子数。对局中它是该方最终得分的下限，终局时即最终得分。
- 即使某方锁定格数已过半，对局仍要到没有空格才结束。
- 同一棋子的多个被堵方向都是合法操作，`hr_legal_moves` 分别列出。它们结果相同，搜索时可对 `hr_ray_length == 0` 的扫略按起点去重。

## 给搜索程序

- `hr_game` 是普通结构体（约 3 KB），`hr_game child = parent;` 即完成复制。
- 也可以用 `hr_apply` / `hr_undo` 原地搜索。
- `HR_MAX_MOVES` 可作为走法缓冲区大小。
- 历史记录不是局面的一部分。做置换表时，应按各格状态和行动方生成键值（手中棋子数可由盘面和 `m` 推出）。
- 保存棋谱：记下 `n`、`m` 和 `hr_history_move` 取出的操作序列，重放即可恢复。

## 线程安全

不同的 `hr_game` 对象互不影响。同一对象被多个线程修改时需自行加锁。

## 测试

`tests/test_hexreclaim.c` 包含各条规则的用例，以及半径 1–8 的 4152 局随机对拍：每一步都与一个独立编写的朴素模型比较，终局后检查重放和逐步撤销。
