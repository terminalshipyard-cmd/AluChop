/**
 * @file MenuPage.cpp
 * @brief Screen 2 — browse, search, sort and filter the 14-category menu; item CRUD.
 * @author Shashank Bhattarai (ACE082BCT078)
 *
 * Every listing on this screen is produced by exactly one service call —
 * services::MenuService::search(term, category, availableOnly, sort) — so the search box, the
 * category filter, the availability filter and the sort selector all funnel through a single
 * entry point and this screen never sorts or filters in memory.
 *
 * @warning Menu prices are already tax-inclusive. Nothing here adds tax to a price; prices are
 *          only ever *formatted* through core::formatNpr.
 */

#include "aluchop/gui/MenuPage.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEasingCurve>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRect>
#include <QSignalBlocker>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

#include "aluchop/core/AppInfo.hpp"
#include "aluchop/core/Money.hpp"
#include "aluchop/gui/ThemeManager.hpp"
#include "aluchop/gui/Widgets.hpp"
#include "aluchop/models/MenuItem.hpp"
#include "aluchop/services/AppContext.hpp"

namespace aluchop::gui {
namespace {

constexpr int kItemIdRole = Qt::UserRole;         ///< menu_items.id stored on the name cell
constexpr int kAvailableRole = Qt::UserRole + 1;  ///< current availability, for the toggle

// ---------------------------------------------------------------------------
// Presentation helpers
// ---------------------------------------------------------------------------

QLabel* styledLabel(const QString& text, const QString& objectName, QWidget* parent,
                    int pointDelta = 0, QFont::Weight weight = QFont::Normal) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    QFont f = label->font();
    if (pointDelta != 0) f.setPointSize(std::max(8, f.pointSize() + pointDelta));
    f.setWeight(weight);
    label->setFont(f);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& objectName, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(objectName);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(36);
    return b;
}

/// Tags a widget for later lookup. A dynamic property is used rather than objectName because
/// objectName is ThemeManager's QSS selector — renaming a label would strip its styling.
void tagWidget(QWidget* w, const QString& name) { w->setProperty("aluchopTag", name); }

/// @oop-concept Function Template :: one lookup serves every tagged widget type on this screen
template <typename T>
T findTagged(const QWidget* root, const QString& name) {
    const auto candidates = root->template findChildren<T>();
    for (T w : candidates)
        if (w->property("aluchopTag").toString() == name) return w;
    return nullptr;
}

/// Linear blend between two palette colours — used to derive per-category tints.
QColor blend(const QColor& a, const QColor& b, qreal t) {
    return QColor(static_cast<int>(a.red() + (b.red() - a.red()) * t),
                  static_cast<int>(a.green() + (b.green() - a.green()) * t),
                  static_cast<int>(a.blue() + (b.blue() - a.blue()) * t));
}

/// A stable sage tint per category, so every "Pizza" thumbnail looks like every other one.
QColor categoryTint(const QString& category, const Palette& pal) {
    const auto hash = static_cast<unsigned>(qHash(category));
    const qreal t = static_cast<qreal>(hash % 100u) / 100.0;
    return blend(pal.accent, pal.primary, t * 0.75);
}

/// @return true when the dish reads as vegetarian (no meat or seafood word anywhere).
bool looksVegetarian(const models::MenuItem& item) {
    static const QStringList meat{
        QStringLiteral("chicken"), QStringLiteral("mutton"), QStringLiteral("pork"),
        QStringLiteral("buff"),    QStringLiteral("beef"),   QStringLiteral("lamb"),
        QStringLiteral("fish"),    QStringLiteral("prawn"),  QStringLiteral("shrimp"),
        QStringLiteral("salmon"),  QStringLiteral("tuna"),   QStringLiteral("crab"),
        QStringLiteral("squid"),   QStringLiteral("bacon"),  QStringLiteral("ham"),
        QStringLiteral("sausage"), QStringLiteral("pepperoni"), QStringLiteral("meat"),
        QStringLiteral("keema"),   QStringLiteral("sekuwa"), QStringLiteral("eel"),
        QStringLiteral("unagi"),   QStringLiteral("ebi"),    QStringLiteral("sashimi")};
    const QString haystack = item.name() + QLatin1Char(' ') + item.description();
    for (const QString& word : meat)
        if (haystack.contains(word, Qt::CaseInsensitive)) return false;
    return true;
}

/// @return true when the dish advertises heat.
bool looksSpicy(const models::MenuItem& item) {
    static const QStringList heat{
        QStringLiteral("chilli"), QStringLiteral("chili"),  QStringLiteral("spicy"),
        QStringLiteral("szechuan"), QStringLiteral("sichuan"), QStringLiteral("jalapeno"),
        QStringLiteral("peri"),   QStringLiteral("wasabi"), QStringLiteral("kimchi"),
        QStringLiteral("hot &"),  QStringLiteral("fiery"),  QStringLiteral("piri")};
    const QString haystack = item.name() + QLatin1Char(' ') + item.description();
    for (const QString& word : heat)
        if (haystack.contains(word, Qt::CaseInsensitive)) return true;
    return false;
}

/// Categories where a veg / spicy marker is meaningful.
bool isFoodCategory(const QString& category) {
    return category != QLatin1String("Drinks") && category != QLatin1String("Beer") &&
           category != QLatin1String("Wine") && category != QLatin1String("Mocktails") &&
           category != QLatin1String("Shots") && category != QLatin1String("Dessert");
}

/**
 * @brief A 48x48 rounded thumbnail for a dish.
 *
 * Photographs are optional on disk. When the file is missing the thumbnail is *generated* —
 * the category's initial on its own sage tint — so the table never shows a broken-image box.
 * Dietary markers are painted as small dots in the corner.
 */
QPixmap thumbnailFor(const models::MenuItem& item, const Palette& pal) {
    const int side = 48;
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, side, side), 12, 12);
    p.setClipPath(clip);

    bool painted = false;
    if (!item.imagePath().isEmpty() && QFile::exists(item.imagePath())) {
        QPixmap photo(item.imagePath());
        if (!photo.isNull()) {
            photo = photo.scaled(side, side, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
            p.drawPixmap((side - photo.width()) / 2, (side - photo.height()) / 2, photo);
            painted = true;
        }
    }

    if (!painted) {
        const QColor tint = categoryTint(item.category(), pal);
        p.fillRect(0, 0, side, side, tint);
        const QString initial = item.category().isEmpty()
                                    ? QStringLiteral("?")
                                    : item.category().left(1).toUpper();
        QFont f = p.font();
        f.setPointSize(18);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(pal.card);
        p.drawText(QRect(0, 0, side, side), Qt::AlignCenter, initial);
    }

    p.setClipping(false);
    if (isFoodCategory(item.category())) {
        int x = side - 12;
        if (looksSpicy(item)) {
            p.setPen(QPen(pal.card, 1.5));
            p.setBrush(pal.danger);
            p.drawEllipse(QRect(x - 4, side - 12, 8, 8));
            x -= 11;
        }
        p.setPen(QPen(pal.card, 1.5));
        p.setBrush(looksVegetarian(item) ? pal.success : pal.secondary);
        p.drawEllipse(QRect(x - 4, side - 12, 8, 8));
    }
    p.end();
    return pm;
}

/// Human summary of the dietary markers, used as a tooltip.
QString markerText(const models::MenuItem& item) {
    if (!isFoodCategory(item.category())) return QString();
    QStringList marks;
    marks << (looksVegetarian(item) ? QStringLiteral("Vegetarian") : QStringLiteral("Contains meat"));
    if (looksSpicy(item)) marks << QStringLiteral("Spicy");
    return marks.join(QStringLiteral(" · "));
}

// ---------------------------------------------------------------------------
// Money entry without ever touching a double
// ---------------------------------------------------------------------------

/**
 * @brief Parses "1,250.50" / "Rs 1250" into core::Money.
 * @return false when the text is not a well-formed, non-negative amount.
 *
 * Deliberately string-based: routing user input through a double would reintroduce exactly the
 * rounding drift core::Money exists to prevent.
 */
bool parseNpr(const QString& raw, core::Money& out) {
    QString text = raw.trimmed();
    text.remove(QStringLiteral("Rs"), Qt::CaseInsensitive);
    text.remove(QLatin1Char(','));
    text.remove(QLatin1Char(' '));
    if (text.isEmpty() || text.startsWith(QLatin1Char('-'))) return false;

    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() > 2) return false;

    bool ok = false;
    const qlonglong rupees = parts.at(0).isEmpty() ? 0 : parts.at(0).toLongLong(&ok);
    if (!parts.at(0).isEmpty() && !ok) return false;

    qlonglong paisa = 0;
    if (parts.size() == 2 && !parts.at(1).isEmpty()) {
        QString frac = parts.at(1);
        if (frac.size() > 2) return false;
        while (frac.size() < 2) frac.append(QLatin1Char('0'));
        paisa = frac.toLongLong(&ok);
        if (!ok) return false;
    }
    out = core::Money::fromRupees(rupees, paisa);
    return true;
}

// ---------------------------------------------------------------------------
// The add / edit dialog
// ---------------------------------------------------------------------------

/**
 * @brief Modal editor for one dish.
 * @param item edited in place; only touched when the user accepts.
 * @return true when the user accepted and every setter validated.
 *
 * The model's setters are the validators (they throw core::ValidationException), so the dialog
 * applies them inside a try block and reports the failure inline instead of duplicating rules.
 */
bool editItemDialog(QWidget* parent, models::MenuItem& item, bool isNew) {
    QDialog dlg(parent);
    dlg.setWindowTitle(isNew ? QStringLiteral("New dish") : QStringLiteral("Edit dish"));
    dlg.setMinimumWidth(480);

    auto* column = new QVBoxLayout(&dlg);
    column->setContentsMargins(22, 20, 22, 18);
    column->setSpacing(14);

    column->addWidget(styledLabel(isNew ? QStringLiteral("Add a dish to the menu")
                                        : QStringLiteral("Edit “%1”").arg(item.name()),
                                  QStringLiteral("sectionTitle"), &dlg, 3, QFont::DemiBold));

    auto* form = new QFormLayout();
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* name = new QLineEdit(item.name(), &dlg);
    name->setMinimumHeight(36);

    auto* category = new QComboBox(&dlg);
    category->setMinimumHeight(36);
    for (const char* c : core::kMenuCategories) category->addItem(QString::fromUtf8(c));
    const int catIndex = category->findText(item.category());
    category->setCurrentIndex(catIndex >= 0 ? catIndex : 0);

    auto* price = new QLineEdit(&dlg);
    price->setMinimumHeight(36);
    price->setPlaceholderText(QStringLiteral("e.g. 450.00"));
    if (item.price().paisa() != 0 || !isNew)
        price->setText(QString::number(item.price().paisa() / 100) + QLatin1Char('.') +
                       QString::number(item.price().paisa() % 100).rightJustified(2, QLatin1Char('0')));

    auto* description = new QPlainTextEdit(item.description(), &dlg);
    description->setMinimumHeight(84);

    auto* image = new QLineEdit(item.imagePath(), &dlg);
    image->setMinimumHeight(36);
    image->setPlaceholderText(QStringLiteral("optional — a placeholder is drawn when empty"));
    auto* browse = makeButton(QStringLiteral("Browse…"), QStringLiteral("ghostButton"), &dlg);
    auto* imageRow = new QHBoxLayout();
    imageRow->setSpacing(8);
    imageRow->addWidget(image, 1);
    imageRow->addWidget(browse, 0);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&dlg, image]() {
        const QString path = QFileDialog::getOpenFileName(
            &dlg, QStringLiteral("Choose a dish photograph"), QString(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
        if (!path.isEmpty()) image->setText(path);
    });

    auto* available = new QCheckBox(QStringLiteral("Currently being served"), &dlg);
    available->setChecked(isNew ? true : item.isAvailable());

    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Category"), category);
    form->addRow(QStringLiteral("Price (NPR, tax-inclusive)"), price);
    form->addRow(QStringLiteral("Description"), description);
    form->addRow(QStringLiteral("Image"), imageRow);
    form->addRow(QString(), available);
    column->addLayout(form);

    auto* error = styledLabel(QString(), QStringLiteral("mutedLabel"), &dlg, -1);
    error->setWordWrap(true);
    error->setVisible(false);
    column->addWidget(error);

    auto* buttons = new QDialogButtonBox(&dlg);
    auto* save = buttons->addButton(isNew ? QStringLiteral("Add dish") : QStringLiteral("Save"),
                                    QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("primaryButton"));
    save->setCursor(Qt::PointingHandCursor);
    save->setMinimumHeight(36);
    auto* cancel = buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("ghostButton"));
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setMinimumHeight(36);
    column->addWidget(buttons);

    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dlg, [&]() {
        core::Money amount;
        if (!parseNpr(price->text(), amount)) {
            error->setText(QStringLiteral("Enter the price as a plain amount, e.g. 450 or 450.50."));
            error->setVisible(true);
            price->setFocus();
            return;
        }
        try {
            /// @oop-concept try / catch :: the model's validating setters are the single source of
            /// truth for what a legal dish is; the dialog just reports what they refuse
            item.setName(name->text());
            item.setCategory(category->currentText());
            item.setPrice(amount);
            item.setDescription(description->toPlainText());
            item.setImagePath(image->text().trimmed());
            item.setAvailable(available->isChecked());
        } catch (const std::exception& e) {
            error->setText(QString::fromUtf8(e.what()));
            error->setVisible(true);
            return;
        }
        dlg.accept();
    });

    name->setFocus();
    return dlg.exec() == QDialog::Accepted;
}

// ---------------------------------------------------------------------------
// The one query that drives the whole screen
// ---------------------------------------------------------------------------

/// Runs MenuService::search with the current control state and repaints the table.
void runQuery(services::AppContext& ctx, QWidget* page, QLineEdit* search, QComboBox* category,
              QComboBox* sort, QTableWidget* table) {
    const Palette& pal = ThemeManager::instance().palette();
    auto* stack = page->findChild<QStackedWidget*>(QStringLiteral("menuStack"));
    auto* countLabel = findTagged<QLabel*>(page, QStringLiteral("menuCount"));
    auto* availableOnly = page->findChild<QCheckBox*>(QStringLiteral("menuAvailableOnly"));

    const QString term = search ? search->text().trimmed() : QString();
    const QString categoryFilter = category ? category->currentData().toString() : QString();
    const auto sortMode = sort ? static_cast<services::MenuSort>(sort->currentData().toInt())
                               : services::MenuSort::NameAsc;
    const bool onlyAvailable = availableOnly && availableOnly->isChecked();

    std::vector<models::MenuItem> items;
    try {
        items = ctx.menu().search(term, categoryFilter, onlyAvailable, sortMode);
    } catch (const std::exception& e) {
        ctx.notifications().notify(QStringLiteral("Menu"), QString::fromUtf8(e.what()), 3);
        return;
    }

    table->setUpdatesEnabled(false);
    table->setRowCount(0);
    int row = 0;
    for (const models::MenuItem& item : items) {
        table->insertRow(row);

        auto* nameCell = new QTableWidgetItem(item.name());
        nameCell->setIcon(QIcon(thumbnailFor(item, pal)));
        nameCell->setData(kItemIdRole, item.id());
        nameCell->setData(kAvailableRole, item.isAvailable());
        QFont nf = nameCell->font();
        nf.setWeight(QFont::DemiBold);
        nameCell->setFont(nf);
        const QString markers = markerText(item);
        nameCell->setToolTip(markers.isEmpty() ? item.name()
                                               : QStringLiteral("%1\n%2").arg(item.name(), markers));
        table->setItem(row, 0, nameCell);

        auto* categoryCell = new QTableWidgetItem(item.category());
        categoryCell->setForeground(pal.textMuted);
        table->setItem(row, 1, categoryCell);

        auto* priceCell = new QTableWidgetItem(core::formatNpr(item.price()));
        priceCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        priceCell->setToolTip(QStringLiteral("Tax-inclusive menu price"));
        table->setItem(row, 2, priceCell);

        auto* availabilityCell = new QTableWidgetItem(
            item.isAvailable() ? QStringLiteral("Available") : QStringLiteral("Off menu"));
        availabilityCell->setForeground(item.isAvailable() ? pal.success : pal.danger);
        QFont af = availabilityCell->font();
        af.setWeight(QFont::DemiBold);
        availabilityCell->setFont(af);
        availabilityCell->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 3, availabilityCell);

        auto* descriptionCell = new QTableWidgetItem(item.description());
        descriptionCell->setForeground(pal.textMuted);
        descriptionCell->setToolTip(item.description());
        table->setItem(row, 4, descriptionCell);

        ++row;
    }
    table->setUpdatesEnabled(true);

    if (countLabel) {
        countLabel->setText(items.empty()
                                ? QStringLiteral("No matching dishes")
                                : QStringLiteral("%1 dish%2")
                                      .arg(items.size())
                                      .arg(items.size() == 1 ? QString() : QStringLiteral("es")));
    }
    if (stack) stack->setCurrentIndex(items.empty() ? 1 : 0);
}

/// A short, subtle fade used when the whole screen reloads.
void fadeIn(QWidget* target, int durationMs = 180) {
    auto* effect = new QGraphicsOpacityEffect(target);
    target->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", target);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);
    QObject::connect(anim, &QPropertyAnimation::finished, target, [target]() {
        // Drop the effect once it has served its purpose; doing it on the next event-loop turn
        // keeps the animation from deleting the object it is still unwinding from.
        QTimer::singleShot(0, target, [target]() { target->setGraphicsEffect(nullptr); });
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MenuPage::MenuPage(services::AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    setObjectName(QStringLiteral("menuPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(16);

    // --- header ------------------------------------------------------------
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);
    auto* headings = new QVBoxLayout();
    headings->setSpacing(2);
    headings->addWidget(styledLabel(pageTitle(), QStringLiteral("pageTitle"), this, 10,
                                    QFont::DemiBold));
    headings->addWidget(styledLabel(
        QStringLiteral("Fourteen sections, every price inclusive of tax."),
        QStringLiteral("mutedLabel"), this));
    headerRow->addLayout(headings, 1);

    auto* addBtn = makeButton(QStringLiteral("+  New dish"), QStringLiteral("primaryButton"), this);
    connect(addBtn, &QPushButton::clicked, this, &MenuPage::onAddItem);
    headerRow->addWidget(addBtn, 0, Qt::AlignVCenter);
    root->addLayout(headerRow);

    // --- filter bar --------------------------------------------------------
    auto* filterPanel = new GlassPanel(this);
    filterPanel->setObjectName(QStringLiteral("card"));
    auto* filterRow = new QHBoxLayout(filterPanel);
    filterRow->setContentsMargins(16, 12, 16, 12);
    filterRow->setSpacing(12);

    m_search = new SearchBar(QStringLiteral("Search a dish, a description or a section…"),
                             filterPanel);
    filterRow->addWidget(m_search, 1);

    m_category = new QComboBox(filterPanel);
    m_category->setMinimumHeight(38);
    m_category->setMinimumWidth(170);
    m_category->setCursor(Qt::PointingHandCursor);
    filterRow->addWidget(m_category, 0);

    m_sort = new QComboBox(filterPanel);
    m_sort->setMinimumHeight(38);
    m_sort->setMinimumWidth(150);
    m_sort->setCursor(Qt::PointingHandCursor);
    m_sort->addItem(QStringLiteral("Name A–Z"), static_cast<int>(services::MenuSort::NameAsc));
    m_sort->addItem(QStringLiteral("Name Z–A"), static_cast<int>(services::MenuSort::NameDesc));
    m_sort->addItem(QStringLiteral("Price low → high"),
                    static_cast<int>(services::MenuSort::PriceAsc));
    m_sort->addItem(QStringLiteral("Price high → low"),
                    static_cast<int>(services::MenuSort::PriceDesc));
    m_sort->addItem(QStringLiteral("Section"), static_cast<int>(services::MenuSort::Category));
    filterRow->addWidget(m_sort, 0);

    auto* availableOnly = new QCheckBox(QStringLiteral("Available only"), filterPanel);
    availableOnly->setObjectName(QStringLiteral("menuAvailableOnly"));
    availableOnly->setCursor(Qt::PointingHandCursor);
    filterRow->addWidget(availableOnly, 0);
    root->addWidget(filterPanel);

    // --- results -----------------------------------------------------------
    auto* resultsPanel = new GlassPanel(this);
    resultsPanel->setObjectName(QStringLiteral("card"));
    auto* resultsColumn = new QVBoxLayout(resultsPanel);
    resultsColumn->setContentsMargins(16, 14, 16, 14);
    resultsColumn->setSpacing(12);

    auto* resultsHeader = new QHBoxLayout();
    resultsHeader->setSpacing(8);
    auto* countLabel = styledLabel(QString(), QStringLiteral("mutedLabel"), resultsPanel);
    tagWidget(countLabel, QStringLiteral("menuCount"));
    resultsHeader->addWidget(countLabel, 1);

    auto* toggleBtn = makeButton(QStringLiteral("Toggle availability"),
                                 QStringLiteral("ghostButton"), resultsPanel);
    connect(toggleBtn, &QPushButton::clicked, this, &MenuPage::onToggleAvailability);
    auto* editBtn = makeButton(QStringLiteral("Edit"), QStringLiteral("ghostButton"), resultsPanel);
    connect(editBtn, &QPushButton::clicked, this, &MenuPage::onEditItem);
    auto* deleteBtn = makeButton(QStringLiteral("Delete"), QStringLiteral("dangerButton"),
                                 resultsPanel);
    connect(deleteBtn, &QPushButton::clicked, this, &MenuPage::onDeleteItem);
    resultsHeader->addWidget(toggleBtn);
    resultsHeader->addWidget(editBtn);
    resultsHeader->addWidget(deleteBtn);
    resultsColumn->addLayout(resultsHeader);

    auto* stack = new QStackedWidget(resultsPanel);
    stack->setObjectName(QStringLiteral("menuStack"));

    m_table = new ElegantTable(
        QStringList{QStringLiteral("Dish"), QStringLiteral("Section"), QStringLiteral("Price"),
                    QStringLiteral("Status"), QStringLiteral("Description")},
        stack);
    m_table->setSortingEnabled(false);  // the service owns the ordering, not the header
    m_table->setIconSize(QSize(44, 44));
    m_table->verticalHeader()->setDefaultSectionSize(60);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->resizeSection(0, 300);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { onEditItem(); });
    stack->addWidget(m_table);

    auto* empty = new EmptyState(QStringLiteral("Nothing matches that search"),
                                 QStringLiteral("Try a different word, clear the section filter, "
                                                "or add the dish to the menu."),
                                 stack);
    QPushButton* emptyAction = empty->enableAction(QStringLiteral("Add a dish"));
    emptyAction->setCursor(Qt::PointingHandCursor);
    connect(emptyAction, &QPushButton::clicked, this, &MenuPage::onAddItem);
    stack->addWidget(empty);

    resultsColumn->addWidget(stack, 1);
    root->addWidget(resultsPanel, 1);

    // --- wiring ------------------------------------------------------------
    auto* debounce = new QTimer(this);
    debounce->setObjectName(QStringLiteral("menuSearchDebounce"));
    debounce->setSingleShot(true);
    debounce->setInterval(220);
    connect(debounce, &QTimer::timeout, this, &MenuPage::onSearch);
    connect(m_search, &QLineEdit::textChanged, this, [debounce](const QString&) { debounce->start(); });
    connect(m_search, &QLineEdit::returnPressed, this, &MenuPage::onSearch);
    connect(m_category, &QComboBox::currentIndexChanged, this, &MenuPage::onCategoryChanged);
    connect(m_sort, &QComboBox::currentIndexChanged, this, &MenuPage::onSortChanged);
    connect(availableOnly, &QCheckBox::toggled, this, [this](bool) { onSearch(); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MenuPage::onSearch);

    refresh();
}

QString MenuPage::pageTitle() const { return QStringLiteral("Menu"); }

// ---------------------------------------------------------------------------
// Refresh and filters
// ---------------------------------------------------------------------------

void MenuPage::refresh() {
    try {
        const QString previous = m_category->currentData().toString();
        {
            const QSignalBlocker block(m_category);
            m_category->clear();
            m_category->addItem(QStringLiteral("All sections"), QString());
            for (const QString& c : m_ctx.menu().categories()) m_category->addItem(c, c);
            const int index = previous.isEmpty() ? 0 : m_category->findData(previous);
            m_category->setCurrentIndex(index >= 0 ? index : 0);
        }
        runQuery(m_ctx, this, m_search, m_category, m_sort, m_table);
        if (auto* stack = findChild<QStackedWidget*>(QStringLiteral("menuStack"))) fadeIn(stack);
    } catch (const std::exception& e) {
        m_ctx.notifications().notify(QStringLiteral("Menu"), QString::fromUtf8(e.what()), 3);
    }
}

void MenuPage::onSearch() { runQuery(m_ctx, this, m_search, m_category, m_sort, m_table); }

void MenuPage::onCategoryChanged() { runQuery(m_ctx, this, m_search, m_category, m_sort, m_table); }

void MenuPage::onSortChanged() { runQuery(m_ctx, this, m_search, m_category, m_sort, m_table); }

// ---------------------------------------------------------------------------
// Item actions
// ---------------------------------------------------------------------------

int MenuPage::selectedItemId() const {
    const int row = m_table->currentRow();
    if (row < 0) return 0;
    const QTableWidgetItem* cell = m_table->item(row, 0);
    return cell ? cell->data(kItemIdRole).toInt() : 0;
}

void MenuPage::onToggleAvailability() {
    const int row = m_table->currentRow();
    const QTableWidgetItem* cell = row >= 0 ? m_table->item(row, 0) : nullptr;
    if (!cell) {
        m_ctx.notifications().notify(QStringLiteral("Menu"),
                                     QStringLiteral("Select a dish first."), 0);
        return;
    }
    const int id = cell->data(kItemIdRole).toInt();
    const bool nowAvailable = cell->data(kAvailableRole).toBool();

    /// @oop-concept Runtime Polymorphism :: the toggle is dispatched as an abstract
    /// services::Command, which is exactly what makes it undoable with Ctrl+Z
    auto command = std::make_unique<services::ToggleAvailabilityCommand>(m_ctx.menu(), id,
                                                                        !nowAvailable);
    const core::Result<void> result = m_ctx.commands().run(std::move(command));
    if (result.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Menu"), result.error(), 3);
        return;
    }
    m_ctx.notifications().notify(
        QStringLiteral("Menu updated"),
        nowAvailable ? QStringLiteral("“%1” is off the menu.").arg(cell->text())
                     : QStringLiteral("“%1” is back on the menu.").arg(cell->text()),
        1);
    onSearch();
    if (row < m_table->rowCount()) m_table->selectRow(row);
}

void MenuPage::onAddItem() {
    models::MenuItem item;
    if (!editItemDialog(this, item, true)) return;

    const core::Result<int> created = m_ctx.menu().create(item);
    if (created.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not add the dish"), created.error(), 3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Dish added"),
                                 QStringLiteral("“%1” is now on the menu.").arg(item.name()), 1);
    refresh();
}

void MenuPage::onEditItem() {
    const int id = selectedItemId();
    if (id == 0) {
        m_ctx.notifications().notify(QStringLiteral("Menu"),
                                     QStringLiteral("Select a dish to edit."), 0);
        return;
    }

    models::MenuItem item;
    bool found = false;
    for (const models::MenuItem& candidate : m_ctx.menu().all()) {
        if (candidate.id() == id) {
            item = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        m_ctx.notifications().notify(QStringLiteral("Menu"),
                                     QStringLiteral("That dish is no longer on the menu."), 2);
        refresh();
        return;
    }

    if (!editItemDialog(this, item, false)) return;

    const core::Result<void> saved = m_ctx.menu().update(item);
    if (saved.isErr()) {
        m_ctx.notifications().notify(QStringLiteral("Could not save the dish"), saved.error(), 3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Dish saved"),
                                 QStringLiteral("“%1” has been updated.").arg(item.name()), 1);
    refresh();
}

void MenuPage::onDeleteItem() {
    const int row = m_table->currentRow();
    const QTableWidgetItem* cell = row >= 0 ? m_table->item(row, 0) : nullptr;
    if (!cell) {
        m_ctx.notifications().notify(QStringLiteral("Menu"),
                                     QStringLiteral("Select a dish to remove."), 0);
        return;
    }
    const QString name = cell->text();
    const int id = cell->data(kItemIdRole).toInt();

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Remove dish"),
        QStringLiteral("Remove “%1” from the menu?\n\nOrders that already contain it keep their "
                       "own name and price snapshot.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    const core::Result<void> removed = m_ctx.menu().remove(id);
    if (removed.isErr()) {
        // A dish referenced by an open order cannot go — that is a service rule, surfaced calmly.
        m_ctx.notifications().notify(QStringLiteral("Could not remove the dish"), removed.error(), 3);
        return;
    }
    m_ctx.notifications().notify(QStringLiteral("Dish removed"),
                                 QStringLiteral("“%1” is no longer on the menu.").arg(name), 1);
    refresh();
}

} // namespace aluchop::gui
