#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSettings>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flashthread.h"
#include "readthread.h"
#include "serial_interface.h"

class HexViewWidget;
class BankSelector;
class QLabel;
class QComboBox;
class QGroupBox;
class QMenu;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class LogWindow;
class SettingsWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    using SerialInterfaceFactory = std::function<std::shared_ptr<SerialInterface>(const std::string&)>;

    MainWindow(const std::shared_ptr<QStringList> log_messages,
               QWidget* parent = nullptr,
               SerialInterfaceFactory serial_interface_factory = nullptr);
    ~MainWindow() override;

private:
    QLabel* label_data_descriptor = nullptr;
    HexViewWidget* hex_widget = nullptr;
    QLabel* label_serial = nullptr;
    QLabel* label_board_id = nullptr;
    QLabel* label_chip_type = nullptr;
    QComboBox* combobox_serial_ports = nullptr;
    QPushButton* button_scan_ports = nullptr;
    QPushButton* button_select_serial = nullptr;
    QPushButton* button_identify_chip = nullptr;
    QPushButton* button_install_firmware = nullptr;
    QPushButton* button_erase_chip = nullptr;
    QPushButton* button_read_rom = nullptr;
    QPushButton* button_flash_rom = nullptr;
    QPushButton* button_read_bank = nullptr;
    QPushButton* button_write_bank = nullptr;
    QPushButton* button_erase_bank = nullptr;
    QPushButton* button_reload_file = nullptr;
    BankSelector* bank_selector = nullptr;
    QProgressBar* progress_bar_load = nullptr;
    QGroupBox* rom_container = nullptr;
    QMenu* recent_files_menu = nullptr;

    std::unique_ptr<LogWindow> log_window;
    std::unique_ptr<SettingsWidget> settings_widget;
    std::shared_ptr<QStringList> log_messages;
    std::shared_ptr<SerialInterface> serial_interface;
    std::unique_ptr<ReadThread> readerthread;
    std::unique_ptr<FlashThread> flashthread;
    SerialInterfaceFactory serial_interface_factory;

    QSettings settings;
    QElapsedTimer operation_timer;
    QString current_filename;
    QString complete_read_filename;
    QByteArray flash_data;
    enum class FlashScope { CompleteRom, Bank };
    FlashScope flash_scope = FlashScope::CompleteRom;
    unsigned int active_bank = 0;
    bool board_connected = false;
    bool chip_identified = false;

    void create_dropdown_menu();
    void build_serial_interface_menu(QVBoxLayout* target_layout);
    void build_rom_selection_menu(QVBoxLayout* target_layout);
    void build_operations_menu(QVBoxLayout* target_layout);
    bool open_file(const QString& filename);
    void add_recent_file(const QString& filename);
    void update_recent_files_menu();
    void show_data(const QString& name, const QByteArray& data);
    void install_firmware_file(const QString& filename,
                               const QString& display_name,
                               bool remember_directory);
    void verify_chip();
    void set_operation_busy(bool busy);
    void raise_error_window(const QString& message);

private slots:
    void exit();
    void slot_open();
    void slot_open_recent_file();
    void slot_reload_file();
    void slot_save();
    void slot_about();
    void slot_debug_log();
    void slot_settings_widget();
    void slot_update_settings();

    void scan_com_devices();
    void select_com_port();
    void load_default_image();

    void read_chip_id();
    void select_firmware_file();
    void install_latest_firmware();
    void read_rom();
    void start_read_rom(const QString& filename);
    void read_bank();
    void read_bank_start(unsigned int bank_id, unsigned int nr_banks);
    void read_bank_done(unsigned int bank_id, unsigned int nr_banks);
    void read_result_ready();
    void bank_read_result_ready();

    void flash_rom();
    void write_bank();
    void flash_block_start(unsigned int block_id, unsigned int nr_blocks);
    void flash_block_done(unsigned int block_id, unsigned int nr_blocks);
    void flash_result_ready();
    void verify_result_ready();
    void erase_chip();
    void erase_bank();
    void thread_abort(const QString& error);
};

#endif // MAINWINDOW_H
