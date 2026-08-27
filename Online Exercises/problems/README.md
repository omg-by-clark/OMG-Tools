# YAML 题库格式

在 `index.yaml` 的 `files` 中登记题包文件名。每个题包必须以 `name:` 开头记录显示名称，随后是 `questions` 数组。

```yaml
name: 示例题集
questions:
  - id: unique-id
    type: single # single / multiple / judge / matching / number / text / cloze / reading / ordering
    title: 题目标题
    prompt: |
      题面支持 **加粗**、``行内代码``、$$x^2$$ 和三反引号代码块。
    options:
      - id: a
        text: 选项 A
      - id: b
        text: 选项 B
    answer: a       # 单选、判断
    answers: [a, b] # 多选
    explanation: 答题后的解析
```

阅读题使用相同的文章题组结构，但不需要挖空标记：

```yaml
- id: reading-example
  type: reading
  title: 阅读理解
  article: Clark is building a website today.
  questions:
    - id: reading-1
      prompt: Clark 正在做什么？
      options:
        - { id: a, text: 建网站 }
        - { id: b, text: 踢足球 }
      answer: a
```

连词成句使用 `type: ordering`，`words` 按显示编号排列，`answer` 保存正确的编号顺序。存在 10 号以上词语时，数字前缀会等待 0.5 秒以区分 `1` 和 `11`：

```yaml
- id: ordering-example
  type: ordering
  title: 连词成句
  prompt: 组成正确的句子。
  words: [today, Clark, coding, is]
  answer: [2, 4, 3, 1]
```

连线题用 `left`、`right` 和映射形式的 `answers`：

```yaml
- id: matching-example
  type: matching
  title: 连线题
  prompt: 连接对应项。
  left:
    - { id: a, text: 左侧 A }
  right:
    - { id: one, text: 右侧 1 }
  answers:
    a: one
```

代码块语言标记支持 `js` / `javascript`、`cpp` / `c++`、`python` / `py`。以反引号开头的 YAML 单行文本需要使用引号包住。

`number` 是数字填空，`text` 是字母填空，两者仅在用户按 Enter 时判题。字母填空默认忽略大小写；需要区分时可设置 `caseSensitive: true`。LaTeX 同时支持 `$...$` 和 `$$...$$`。

完形填空使用 `type: cloze`，文章中的 `{{1}}`、`{{2}}` 会渲染为挖空；每个子题独立计分，当前文章会在所有空完成前始终显示：

```yaml
- id: cloze-example
  type: cloze
  title: 完形填空
  article: Clark {{1}} code now, and he {{2}} it tomorrow.
  questions:
    - id: blank-1
      prompt: 选择第 1 空。
      options:
        - { id: a, text: writes }
        - { id: b, text: is writing }
      answer: b
    - id: blank-2
      prompt: 选择第 2 空。
      options:
        - { id: a, text: will improve }
        - { id: b, text: improves }
      answer: a
```
