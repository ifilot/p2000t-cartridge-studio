#include "bankselector.h"

#include "romsizes.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFontDatabase>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QToolButton>
#include <QWidgetAction>

namespace {

constexpr int BANK_COLUMNS = 3;

} // namespace

BankSelector::BankSelector(QWidget* parent)
    : QPushButton(parent),
      button_group(new QButtonGroup(this)),
      popup_menu(new QMenu(this))
{
    this->setObjectName(QStringLiteral("bankSelector"));
    this->setMinimumHeight(40);
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    this->setIconSize(QSize(64, 30));
    QFont label_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    label_font.setPointSize(8);
    label_font.setWeight(QFont::DemiBold);
    this->setFont(label_font);
    this->setAccessibleName(tr("Selected bank"));

    this->popup_menu->setObjectName(QStringLiteral("menuBankSelector"));
    auto* grid_widget = new QWidget(this->popup_menu);
    grid_widget->setObjectName(QStringLiteral("bankSelectorGrid"));
    auto* grid = new QGridLayout(grid_widget);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);
    grid->setSizeConstraint(QLayout::SetFixedSize);
    grid_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    this->button_group->setExclusive(true);
    for(int bank = 0; bank < NUMBANKS; ++bank) {
        auto* button = new QToolButton(grid_widget);
        button->setObjectName(QStringLiteral("buttonSelectBank%1").arg(bank));
        button->setText(this->bankLabel(bank));
        button->setIcon(this->dipSwitchIcon(bank));
        button->setIconSize(QSize(64, 30));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFont(this->font());
        button->setCheckable(true);
        button->setFixedSize(116, 40);
        button->setToolTip(this->bankDescription(bank));
        button->setAccessibleName(tr("Select bank %1, DIP 1 to 4: %2")
                                      .arg(bank)
                                      .arg(this->dipSwitchPattern(bank)));
        button->setProperty("dipSwitchPattern", this->dipSwitchPattern(bank));
        this->button_group->addButton(button, bank);
        grid->addWidget(button, bank / BANK_COLUMNS, bank % BANK_COLUMNS);
        connect(button, &QToolButton::clicked, this, [this, bank]() {
            this->setCurrentBank(bank);
            this->popup_menu->close();
        });
    }

    auto* grid_action = new QWidgetAction(this->popup_menu);
    grid_action->setDefaultWidget(grid_widget);
    this->popup_menu->addAction(grid_action);
    this->setMenu(this->popup_menu);

    connect(this->popup_menu, &QMenu::aboutToShow, this, [this]() {
        if(auto* selected = this->button_group->button(this->current_bank)) {
            selected->setFocus(Qt::PopupFocusReason);
        }
    });

    this->setCurrentBank(0);
}

int BankSelector::currentBank() const
{
    return this->current_bank;
}

QString BankSelector::bankLabel(int bank) const
{
    return tr("BANK\n%1").arg(bank, 2, 10, QLatin1Char('0'));
}

void BankSelector::setCurrentBank(int bank)
{
    if(bank < 0 || bank >= NUMBANKS || bank == this->current_bank) return;
    this->current_bank = bank;
    this->updatePresentation();
    emit this->currentBankChanged(bank);
}

QString BankSelector::bankDescription(int bank) const
{
    const int start = bank * BANKSIZE;
    const QString range = QStringLiteral("0x%1–0x%2")
        .arg(start, 5, 16, QLatin1Char('0'))
        .arg(start + BANKSIZE - 1, 5, 16, QLatin1Char('0'))
        .toUpper();
    return tr("Bank %1 (%2)\nDIP 1 to 4: %3")
        .arg(bank)
        .arg(range)
        .arg(this->dipSwitchPattern(bank));
}

QString BankSelector::dipSwitchPattern(int bank) const
{
    QStringList positions;
    positions.reserve(4);
    for(int bit = 0; bit < 4; ++bit) {
        positions.append(bank & (1 << bit) ? tr("ON") : tr("OFF"));
    }
    return positions.join(QLatin1Char(' '));
}

QIcon BankSelector::dipSwitchIcon(int bank) const
{
    constexpr int scale = 2;
    constexpr int width = 72;
    constexpr int height = 34;
    QPixmap pixmap(width * scale, height * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF body(1.0, 1.0, 70.0, 32.0);
    painter.setPen(QPen(QColor(112, 24, 24), 1.0));
    painter.setBrush(QColor(181, 45, 45));
    painter.drawRoundedRect(body, 3.0, 3.0);

    QFont label_font = painter.font();
    label_font.setBold(true);
    label_font.setPixelSize(7);
    painter.setFont(label_font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(2.0, 2.0, 13.0, 7.0), Qt::AlignCenter,
                     QStringLiteral("ON"));

    for(int bit = 0; bit < 4; ++bit) {
        const qreal x = 17.0 + bit * 13.0;
        const QRectF slot(x, 8.0, 9.0, 18.0);
        painter.setPen(QPen(QColor(92, 19, 19), 0.8));
        painter.setBrush(QColor(74, 24, 24));
        painter.drawRoundedRect(slot, 2.0, 2.0);

        const bool on = bank & (1 << bit);
        const QRectF toggle(x + 1.0, on ? 9.0 : 18.0, 7.0, 7.0);
        painter.setPen(QPen(QColor(170, 170, 166), 0.6));
        painter.setBrush(QColor(245, 244, 236));
        painter.drawRoundedRect(toggle, 1.3, 1.3);

        painter.setPen(QColor(255, 220, 220));
        painter.drawText(QRectF(x, 26.0, 9.0, 7.0), Qt::AlignCenter,
                         QString::number(bit + 1));
    }

    return QIcon(pixmap);
}

void BankSelector::updatePresentation()
{
    this->setText(this->bankLabel(this->current_bank));
    this->setIcon(this->dipSwitchIcon(this->current_bank));
    this->setToolTip(this->bankDescription(this->current_bank));
    this->setAccessibleDescription(this->toolTip());
    if(auto* selected = this->button_group->button(this->current_bank)) {
        selected->setChecked(true);
    }
}
