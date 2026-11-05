# 注释约定

## 文件头注释

`.h` 和 `.cpp` 文件沿用库内的文件头格式，放在文件最前面；头文件在文件头之后写 `#pragma once`，实现文件在文件头之后写包含语句。字段保持为 `@Description`、`@Author`、`@Date`、`@LastEditTime`、`@License`，每项单独一行，结束符单独一行。例如：

```cpp
/*
 * @Description: RX8130CE 实时时钟芯片驱动接口
 * @Author: LILYGO_L
 * @Date: 2026-10-31 11:00:00
 * @LastEditTime: 2026-10-31 13:35:42
 * @License: GPL 3.0
 */
#pragma once
```

示例中的描述和时间只用于展示格式，不要复制为新文件的固定内容。描述应对应本文件职责；已有文件保留作者、创建时间和许可证，修改时间按实际修改维护，不伪造时间或作者信息。

## 函数注释

注释的目的是让不了解实现的人能正确使用或维护代码。只写能补足接口用途、参数约束、单位、协议含义或不直观行为的信息；不要复述赋值和显而易见的控制流程。极其简单、含义直接的构造函数、析构函数、初始化/反初始化包装函数通常不需要单独注释，避免注释比代码更复杂。

需要说明的公共函数使用 Doxygen 格式。`@brief` 说明做什么；有输入参数时用 `@param` 说明含义、单位、取值范围和失败时是否修改输出；有返回值时用 `@return` 说明成功/失败或返回值含义。`@note` 是可选项，通常不添加；只有存在无法自然放入 brief、参数或返回值说明中的额外限制或重要行为时才使用。不要为了格式完整而给显而易见的参数重复写废话。

```cpp
/**
 * @brief 连续读取日历寄存器，避免跨秒拼接
 * @param time 接收日期时间；读取失败时保持原值
 * @param voltage_low 接收低电压标志；为 true 时日期虽可读但时间不可靠
 * @return 通信和数据格式有效时返回 true
 * @note 总线事务应在一秒内完成
 */
bool GetTime(Time& time, bool& voltage_low);
```

非常简单的接口可以直接声明，不需要为了“每个函数都有注释”而添加注释：

```cpp
explicit Rx8130ce(std::shared_ptr<I2cBusBase> bus,
    int16_t address = kDefaultI2cAddress)
    : I2cChipBase(bus, address) {}
bool Init(int32_t freq_hz = kDefaultI2cFrequencyHz) override;
bool Deinit(bool delete_bus = true) override;
```

类声明上方通常无需再写一段复述类名的注释。初始化等简单函数若存在不直观的副作用、资源归属或调用顺序约束，仍需说明这些信息；是否注释由内容决定，不仅由函数名决定。

## 普通代码注释

普通说明优先使用 `//`，包括数组元素、特殊取值、位域含义和局部处理原因；多行普通说明也可以连续使用 `//`。不要把普通代码说明都写成 Doxygen 块。

```cpp
const uint8_t write_data[] = {
    0x1F,  // 电源控制寄存器地址
    0x10,  // 启用备用电源自动切换，保持充电关闭
};
```

注释紧邻其描述的声明。带 Doxygen 注释的函数与相邻声明之间留一个空行；没有注释且属于同一组的简单函数通常连续书写，不必每个函数之间都加空行。移动声明时一并移动注释；删除声明时，也删除仅用于说明该声明的注释。如果 `.h` 已完整说明接口，`.cpp` 中无需重复添加相同注释。

## 错误日志

同一失败路径只在最了解失败原因的底层记录一次日志。底层函数已经输出具体错误时，上层只传播失败并返回，不要再输出笼统的“初始化失败”或“读写失败”，否则一处故障会产生多条重复日志。上层只有在增加了新的上下文、且底层没有记录同一失败时，才补充日志。

例如，当前函数先检查自己负责的参数；调用基类失败时直接返回，因为基类已经记录了总线初始化或探测原因：

```cpp
bool Rx8130ce::Init(int32_t freq_hz) {
  if (freq_hz <= 0 || freq_hz > 400000) {
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "RX8130CE invalid I2C frequency\n");
    return false;
  }
  if (!I2cChipBase::Init(freq_hz)) {
    return false;
  }
  return ConfigureClock();
}
```

寄存器读写也遵循相同原则：在统一访问入口记录寄存器地址和传输错误；调用它的状态读取、初始化或业务 API 只返回失败，不为同一 I2C 错误重复记录日志。
