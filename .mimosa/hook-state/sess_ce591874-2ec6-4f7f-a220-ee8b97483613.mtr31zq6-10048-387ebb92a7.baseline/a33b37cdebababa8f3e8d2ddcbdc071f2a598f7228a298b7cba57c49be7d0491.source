#pragma once
// GUI 小工具函数。
#include <QTableWidget>

#include <initializer_list>

// 填充表格一行（按列顺序），并去重设置 item。
inline void setRow(QTableWidget* table, int row, std::initializer_list<QString> cells) {
    int col = 0;
    for (const auto& text : cells) {
        auto* item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, col++, item);
    }
}

// 数量格式化：1234567 -> "1,234,567"
inline QString formatNum(qint64 n) {
    QString s = QString::number(n);
    for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, ',');
    return s;
}
