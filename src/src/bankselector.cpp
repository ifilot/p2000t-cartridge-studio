#include "bankselector.h"

#include "romsizes.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>

BankSelector::BankSelector(QWidget* parent)
    : QPushButton(parent),
      button_group(new QButtonGroup(this)),
      popup_menu(new QMenu(this))
{
    this->setObjectName(QStringLiteral("bankSelector"));
    this->setMinimumWidth(110);
    this->setAccessibleName(tr("Selected bank"));

    this->popup_menu->setObjectName(QStringLiteral("menuBankSelector"));
    auto* grid_widget = new QWidget(this->popup_menu);
    grid_widget->setObjectName(QStringLiteral("bankSelectorGrid"));
    auto* grid = new QGridLayout(grid_widget);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);

    this->button_group->setExclusive(true);
    for(int bank = 0; bank < NUMBANKS; ++bank) {
        auto* button = new QToolButton(grid_widget);
        button->setObjectName(QStringLiteral("buttonSelectBank%1").arg(bank));
        button->setText(QString::number(bank));
        button->setCheckable(true);
        button->setFixedSize(40, 34);
        button->setToolTip(this->bankDescription(bank));
        button->setAccessibleName(tr("Select bank %1").arg(bank));
        this->button_group->addButton(button, bank);
        grid->addWidget(button, bank / 4, bank % 4);
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
    return tr("Bank %1 (%2)").arg(bank).arg(range);
}

void BankSelector::updatePresentation()
{
    this->setText(tr("Bank %1").arg(this->current_bank));
    this->setToolTip(this->bankDescription(this->current_bank));
    this->setAccessibleDescription(this->toolTip());
    if(auto* selected = this->button_group->button(this->current_bank)) {
        selected->setChecked(true);
    }
}
