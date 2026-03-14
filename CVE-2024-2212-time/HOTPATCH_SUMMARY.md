# nRF52840 Hotpatch 修复总结

## 目标

修复当前 PlatformIO 工程，只保留并验证一次完整的热修复流程，使程序启动后输出：

```text
this is fun1
this is fun2
this is fun1
```

对应语义：

1. 初始状态执行 `fun1`
2. 应用热补丁后执行 `fun2`
3. 卸载热补丁后再次执行 `fun1`

## 根因总结

本次问题不在于板子或 J-Link 无法运行，而在于当前工程的热补丁演示逻辑存在两个关键点：

1. `fun2()` 原先会在输出后跳回 `patch_slot + 2`，继续落回 `fun1()` 路径
2. `.hotpatch_page` 虽然单独分段，但没有被固定到独立的 4KB Flash 页，擦页/回写的布局不够严格

因此会出现“补丁已写入，但运行效果不符合预期”的现象。

## 修改内容

### 1. 修正热补丁后的执行路径

文件：[src/main.c](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/src/main.c)

- 删除 `g_patch_resume`
- 将 `fun2()` 改为只输出 `this is fun2`，随后直接返回给 `patch_slot()` 的调用者
- 不再回跳到 `patch_slot` 内部继续执行 `fun1`

修复后，补丁态下调用 `patch_slot()` 的效果为：

```text
patch_slot -> fun2 -> return to caller
```

### 2. 收敛启动演示流程

文件：[src/main.c](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/src/main.c)

将 `run_demo()` 保持为一轮最小演示：

1. `patch_slot_reset_to_fun1(); patch_slot();`
2. `patch_slot_redirect_to_fun2(); patch_slot();`
3. `patch_slot_unpatch_to_fun1(); patch_slot();`

这样启动后只输出一轮：

```text
fun1 -> fun2 -> fun1
```

### 3. 固定热补丁页到独立 4KB Flash 页

文件：[linker/uart_gcc_nrf52.ld](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/linker/uart_gcc_nrf52.ld)

将 `.hotpatch_page` 调整为：

- 段起始地址按 `0x1000` 对齐
- 段内部补齐到整页
- 使用 `0xFF` 填充

修复后实际链接结果：

- `patch_slot = 0x00002000`
- `fun2 = 0x0000200C`
- `.hotpatch_page` 大小为 `0x1000`

这满足 nRF52840 按页擦写的基本要求。

## 实测验证

已完成以下验证：

1. `pio run`
2. `pio run -t upload`
3. 使用 J-Link RTT 读取板子启动输出

实际 RTT 输出如下：

```text
RTT console ready.
this is fun1
this is fun2
this is fun1
commands: help, demo, call, patch, unpatch, reset, status
rtt>
```

结果说明：

- 初始调用正常进入 `fun1`
- 应用补丁后正常跳转到 `fun2`
- 卸载补丁后重新回到 `fun1`

## 当前结论

当前 PlatformIO 工程已经实现并验证了“一次热修复的应用与卸载”，行为符合预期。

如果后续还要继续扩展，可以在此基础上再做：

- 更详细的 `status` 状态输出
- 多次补丁/卸载循环测试
- 与 FreeRTOS 版本重新合并
