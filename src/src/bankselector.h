#ifndef BANKSELECTOR_H
#define BANKSELECTOR_H

#include <QPushButton>

class QButtonGroup;
class QMenu;

class BankSelector final : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(int currentBank READ currentBank WRITE setCurrentBank NOTIFY currentBankChanged)

public:
    explicit BankSelector(QWidget* parent = nullptr);

    int currentBank() const;

public slots:
    void setCurrentBank(int bank);

signals:
    void currentBankChanged(int bank);

private:
    QString bankDescription(int bank) const;
    void updatePresentation();

    QButtonGroup* button_group = nullptr;
    QMenu* popup_menu = nullptr;
    int current_bank = -1;
};

#endif // BANKSELECTOR_H
