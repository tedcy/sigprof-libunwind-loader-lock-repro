# 纯 gperftools/libunwind helper 验证 demo

这个 demo 用来验证 `unwind_safeness_helper` 能避免 CPU profiler 的 `SIGPROF` handler 在不安全窗口里继续走 libunwind。

代码只保留 C++ 异常压力，并依赖 gperftools CPU profiler 的定时 `SIGPROF` 采样。业务代码不直接调用 `dl_iterate_phdr`；handler 外层进入 loader lock 来自 C++ 异常展开。

## 编译依赖

```bash
make deps
make
```

`make deps` 会调用 `third_party/CMakeLists.txt` 下载并本地安装：

- gperftools 2.7
- libunwind 1.3.1
- `unwind_safeness_helper`

依赖全部放在本目录 `third_party/` 下。顶层 `Makefile` 链接本地构建的 `libprofiler.a`，并动态链接本地构建的 `libunwind.so`。仓库根目录提交了一份旧 `libgcc_s.so.1`，运行时从当前目录加载它，让 C++ 异常展开稳定进入 `dl_iterate_phdr`。

gperftools 和 libunwind 源码不打补丁。`unwind_safeness_helper.so` 基于 upstream helper，只加一个 local-only 符号兼容 patch：gperftools 2.7 这里调用的是 libunwind 的 `_ULx86_64_*` 符号，所以 helper 需要以 `UNW_LOCAL_ONLY` 编译并拦截同一组符号。启用 helper 时，`LD_PRELOAD` 会包装 `dlopen/dlclose/dl_iterate_phdr` 和 libunwind 的 step/get-reg API；如果当前线程处于 loader 不安全区域，helper 会让本次 libunwind 展开提前结束。

## 验证 patch 生效

这里最容易看漏的是符号名前缀差异：**`_ULx86_64_*` 比 `_Ux86_64_*` 多了一个 `L`**。这个 `L` 表示 libunwind 的 local-only ABI。

| 对象 | 符号前缀 | 含义 |
| --- | --- | --- |
| demo 程序 / gperftools 2.7 | `_ULx86_64_*` | local-only libunwind 符号 |
| upstream helper 原样构建 | `_Ux86_64_*` | 非 local-only libunwind 符号 |
| patched helper | `_ULx86_64_*` | 和 demo 程序匹配的 local-only 符号 |

先确认 demo 程序实际需要的是 `_ULx86_64_*` 这组 local-only 符号：

```bash
nm -D ./minimal_gperftools_libunwind_helper | \
  grep -E '_ULx86_64_(init_local|step|get_reg)'
```

预期能看到这三行未解析符号：

```text
U _ULx86_64_get_reg
U _ULx86_64_init_local
U _ULx86_64_step
```

再确认 patched helper 也导出了同一组 `_ULx86_64_*` 符号：

```bash
nm -D third_party/install/lib/unwind_safeness_helper.so | \
  grep -E '_ULx86_64_(init_local|step|get_reg)'
```

预期能看到这三行导出符号：

```text
T _ULx86_64_get_reg
T _ULx86_64_init_local
T _ULx86_64_step
```

这一步证明 `LD_PRELOAD` 加载的 helper 能拦到 gperftools 2.7 实际调用的 libunwind API。如果 helper 仍然只导出 `_Ux86_64_*`，或者启动时报 `failed to find symbol unw_init_local`，说明 local-only 兼容 patch 没有生效。

作为对照，`make deps` 还会构建一份不打 patch 的 upstream helper。注意这里预期看到的是 **`_Ux86_64_*`，少了 `L`**：

```bash
nm -D third_party/install/lib/unwind_safeness_helper_upstream.so | \
  grep -E '_Ux86_64_(init_local|step|get_reg)|_ULx86_64_(init_local|step|get_reg)'
```

预期只能看到 `_Ux86_64_*` 这组符号：

```text
T _Ux86_64_get_reg
T _Ux86_64_init_local
T _Ux86_64_step
```

这说明原样 helper 没有导出 demo 实际需要拦截的 `_ULx86_64_*`，它导出的 `_Ux86_64_*` 和 demo 程序引用的 `_ULx86_64_*` 不是同一组动态符号。直接运行原样 helper 时，进程会在 helper constructor 阶段失败：

```bash
env LD_PRELOAD="$PWD/third_party/install/lib/unwind_safeness_helper_upstream.so" \
  LD_LIBRARY_PATH="$PWD:$PWD/third_party/install/lib" \
  ./minimal_gperftools_libunwind_helper
```

预期现象：

```text
failed to find symbol unw_init_local: ... undefined symbol: unw_init_local
Aborted
```

## 运行

未启用 helper 的基线：

```bash
env LD_LIBRARY_PATH="$PWD:$PWD/third_party/install/lib" \
  ./minimal_gperftools_libunwind_helper
```

启用 helper：

```bash
env LD_PRELOAD="$PWD/third_party/install/lib/unwind_safeness_helper.so" \
  LD_LIBRARY_PATH="$PWD:$PWD/third_party/install/lib" \
  ./minimal_gperftools_libunwind_helper
```

启用不打 patch 的 upstream helper 对照：

```bash
env LD_PRELOAD="$PWD/third_party/install/lib/unwind_safeness_helper_upstream.so" \
  LD_LIBRARY_PATH="$PWD:$PWD/third_party/install/lib" \
  ./minimal_gperftools_libunwind_helper
```

demo 静态链接本地构建的 gperftools，并动态链接本地构建的 libunwind，运行时不再额外指定 `TCMALLOC_STACKTRACE_METHOD`。

demo 不设置 `CPUPROFILE_FREQUENCY`，使用 gperftools 2.7 默认的 100Hz 采样频率即可复现。实测 1Hz 在当前 20 秒运行窗口内没有复现；4000Hz 只是加快命中危险窗口，不是触发链路的必要条件。

预期对比：

- 基线命令：profiling signal 如果打中异常展开 / loader 不安全窗口，worker 计数应停滞；有时 watchdog 会 abort，有时整个进程会停在 profiler 路径里，需要手动停止。
- upstream helper 命令：原样 helper 没有 local-only 符号兼容，启动阶段应报 `failed to find symbol unw_init_local` 并 abort。
- patched helper 命令：helper 标记不安全区域，并让 libunwind 展开提前结束，worker 计数应持续推进。

helper 的验证重点是：启用 helper 后不应因为 handler 内 libunwind 重入 `dl_iterate_phdr` 而卡死。
