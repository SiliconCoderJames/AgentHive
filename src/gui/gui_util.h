#pragma once
// GUI 小工具函数：表格填充、右键菜单（复制/详情/导出 CSV）、数字格式化。
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>

#include <initializer_list>

// 填充表格一行（按列顺序），并去重设置 item；全文同时挂到 tooltip，
// 列宽不足被截断时悬停即可看到完整内容。
inline void setRow(QTableWidget* table, int row, std::initializer_list<QString> cells) {
    int col = 0;
    for (const auto& text : cells) {
        auto* item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        if (!text.isEmpty()) item->setToolTip(text);
        table->setItem(row, col++, item);
    }
}

// 数量格式化：1234567 -> "1,234,567"
inline QString formatNum(qint64 n) {
    QString s = QString::number(n);
    for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, ',');
    return s;
}

// 表格统一抛光：隔行底色、隐藏垂直表头、整行选择、只读、舒适行高
inline void polishTable(QTableWidget* table) {
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
}

// 表格右键菜单：复制单元格/整行、导出 CSV。idColumn >= 0 时提供"复制 ID"。
inline void attachTableContextMenu(QTableWidget* table, int idColumn = -1) {
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QTableWidget::customContextMenuRequested,
                     table, [table, idColumn](const QPoint& pos) {
                         int row = table->rowAt(pos.y());
                         QMenu menu(table);
                         QAction* copyCell = menu.addAction("复制单元格");
                         QAction* copyRow = menu.addAction("复制整行");
                         QAction* copyId = nullptr;
                         if (idColumn >= 0 && row >= 0)
                             copyId = menu.addAction("复制 ID");
                         menu.addSeparator();
                         QAction* exportCsv = menu.addAction("导出 CSV…");
                         QAction* act = menu.exec(table->viewport()->mapToGlobal(pos));
                         if (!act || row < 0) return;
                         if (act == copyCell) {
                             auto* it = table->item(row, table->columnAt(pos.x()));
                             if (it) QApplication::clipboard()->setText(it->text());
                         } else if (act == copyRow) {
                             QStringList cells;
                             for (int c = 0; c < table->columnCount(); ++c) {
                                 auto* it = table->item(row, c);
                                 cells << (it ? it->text() : "");
                             }
                             QApplication::clipboard()->setText(cells.join("\t"));
                         } else if (copyId && act == copyId) {
                             auto* it = table->item(row, idColumn);
                             if (it) QApplication::clipboard()->setText(it->text());
                         } else if (act == exportCsv) {
                             QString path = QFileDialog::getSaveFileName(
                                 table, "导出 CSV", "export.csv", "CSV (*.csv)");
                             if (path.isEmpty()) return;
                             QFile f(path);
                             if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                                 QMessageBox::warning(table, "导出失败", "无法写入文件");
                                 return;
                             }
                             auto esc = [](QString v) {
                                 v.replace('"', "\"\"");
                                 return "\"" + v + "\"";
                             };
                             QTextStream out(&f);
                             out << "\xEF\xBB\xBF";  // Excel 兼容 BOM（Qt6 默认 UTF-8）
                             for (int c = 0; c < table->columnCount(); ++c)
                                 out << esc(table->horizontalHeaderItem(c)
                                                ? table->horizontalHeaderItem(c)->text()
                                                : "")
                                     << (c + 1 < table->columnCount() ? "," : "\n");
                             for (int r = 0; r < table->rowCount(); ++r) {
                                 for (int c = 0; c < table->columnCount(); ++c) {
                                     auto* it = table->item(r, c);
                                     out << esc(it ? it->text() : "")
                                         << (c + 1 < table->columnCount() ? "," : "\n");
                                 }
                             }
                             QMessageBox::information(table, "导出完成", path);
                         }
                     });
}
