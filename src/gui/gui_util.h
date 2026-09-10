#pragma once
// GUI 小工具函数：表格填充、即时过滤、右键菜单（复制/详情/导出 CSV）、数字格式化。
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>
#include <QTreeWidget>

#include <functional>
#include <initializer_list>
#include <utility>

#include "i18n.h"

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

// 即时过滤（纯函数）：按任意列、大小写不敏感隐藏不匹配的行，空文本恢复全部；
// 过滤只改可见性，不动数据。面板在 refresh() 重建行后需再次调用以保持过滤态。
inline void applyTableFilter(QTableWidget* table, const QString& text) {
    const QString needle = text.trimmed();
    for (int r = 0; r < table->rowCount(); ++r) {
        if (needle.isEmpty()) {
            table->setRowHidden(r, false);
            continue;
        }
        bool hit = false;
        for (int c = 0; c < table->columnCount() && !hit; ++c)
            if (auto* it = table->item(r, c); it && it->text().contains(needle, Qt::CaseInsensitive))
                hit = true;
        table->setRowHidden(r, !hit);
    }
}

namespace detail {
// 过滤输入框的 Esc 行为：按 Esc 清空过滤并恢复全部行（编辑中时优先消费）。
class FilterEditFilter : public QObject {
public:
    using Apply = std::function<void(const QString&)>;
    FilterEditFilter(QLineEdit* edit, Apply apply) : QObject(edit), apply_(std::move(apply)) {
        edit->installEventFilter(this);
    }
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::KeyPress) {
            if (auto* ke = static_cast<QKeyEvent*>(e); ke->key() == Qt::Key_Escape) {
                auto* edit = static_cast<QLineEdit*>(obj);
                if (!edit->text().isEmpty()) {
                    edit->clear();
                    apply_(QString());
                }
                return true;
            }
        }
        return QObject::eventFilter(obj, e);
    }

private:
    Apply apply_;
};
}  // namespace detail

// 即时过滤输入框：文本变化即过滤（任意列、大小写不敏感），Esc 清空恢复。
// 返回输入框交给面板放进工具栏；refresh() 重建行后请调用 applyTableFilter 保持过滤态。
inline QLineEdit* makeTableFilter(QTableWidget* table, QWidget* parent,
                                  const QString& placeholder = {}) {
    auto* edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder.isEmpty()
                                 ? i18n::trs("🔍  输入即筛…", "🔍  Type to filter…")
                                 : placeholder);
    edit->setClearButtonEnabled(true);
    edit->setToolTip(i18n::trs("即时过滤（Ctrl+F 聚焦 · Esc 清空）",
                               "Live filter (Ctrl+F to focus · Esc to clear)"));
    auto apply = [table](const QString& text) { applyTableFilter(table, text); };
    QObject::connect(edit, &QLineEdit::textChanged, table, apply);
    new detail::FilterEditFilter(edit, std::move(apply));
    return edit;
}

// 树形即时过滤（纯函数）：叶子任一列命中则该行与所属分组可见，命中的分组自动展开，
// 其余分组隐藏；空文本恢复全部并收回到默认展开深度。
inline void applyTreeFilter(QTreeWidget* tree, const QString& text) {
    const QString needle = text.trimmed();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* group = tree->topLevelItem(i);
        if (needle.isEmpty()) {
            group->setHidden(false);
            for (int j = 0; j < group->childCount(); ++j)
                group->child(j)->setHidden(false);
            continue;
        }
        bool groupHit = false;
        for (int j = 0; j < group->childCount(); ++j) {
            auto* row = group->child(j);
            bool hit = false;
            for (int c = 0; c < tree->columnCount() && !hit; ++c)
                if (row->text(c).contains(needle, Qt::CaseInsensitive)) hit = true;
            row->setHidden(!hit);
            groupHit |= hit;
        }
        group->setHidden(!groupHit);
        group->setExpanded(groupHit);
    }
}

// 树形过滤输入框：配合 applyTreeFilter 使用，Esc 清空；refresh() 后需重调 applyTreeFilter。
inline QLineEdit* makeTreeFilter(QTreeWidget* tree, QWidget* parent,
                                 const QString& placeholder = {}) {
    auto* edit = new QLineEdit(parent);
    edit->setPlaceholderText(placeholder.isEmpty()
                                 ? i18n::trs("🔍  输入即筛…", "🔍  Type to filter…")
                                 : placeholder);
    edit->setClearButtonEnabled(true);
    edit->setToolTip(i18n::trs("即时过滤（Ctrl+F 聚焦 · Esc 清空）",
                               "Live filter (Ctrl+F to focus · Esc to clear)"));
    auto apply = [tree](const QString& text) { applyTreeFilter(tree, text); };
    QObject::connect(edit, &QLineEdit::textChanged, tree, apply);
    new detail::FilterEditFilter(edit, std::move(apply));
    return edit;
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
