# 提交说明约定

提交说明的标题和正文统一使用英文，采用以下格式：

```text
<type>(<scope>): <summary>

- <change description>
- <change description>
```

- `type` 表示改动类型，例如 `feat`、`fix`、`refactor`、`docs` 或 `chore`。
- `scope` 可选；省略时同时省略括号，格式为 `feat: <summary>`。
- `summary` 简洁描述本次提交的主要改动，使用英文动词原形开头，不加句号。
- 标题与正文之间空一行，正文使用 `- ` 开头的英文列表，每项描述一项实际改动，使用英文动词原形开头。
- 提交说明应与当前提交的实际范围一致。库和应用层分别提交时，应分别编写说明，不混入其他仓库或未包含在该提交中的改动。

示例：

```text
feat: integrate RX8130CE into T-Display-P4-Air

- Initialize RX8130CE on the shared I2C bus
- Enable VBAT power switching and configure RTC charging
- Track RX8130CE initialization status
```

带可选范围的标题示例：

```text
feat(rtc): integrate RX8130CE into T-Display-P4-Air
```

示例仅用于展示格式，提交时应按实际改动填写。

