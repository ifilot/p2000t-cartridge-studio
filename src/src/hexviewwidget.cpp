/****************************************************************************
 *                                                                          *
 *   P2000T Cartridge Studio                                                *
 *   Copyright (C) 2023 Ivo Filot <ivo@ivofilot.nl>                         *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as                *
 *   published by the Free Software Foundation, either version 3 of the     *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   You should have received a copy of the GNU General Public license      *
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>. *
 *                                                                          *
 ****************************************************************************/

#include "hexviewwidget.h"

#include <algorithm>

/**
 * @brief HexViewWidget::HexViewWidget
 * @param parent window
 */
HexViewWidget::HexViewWidget(QWidget *parent)
    : QAbstractScrollArea{parent} {
}

/**
 * @brief set data for the HexView class to display
 * @param _data data to display
 */
void HexViewWidget::set_data(const QByteArray& _data) {
    QMutexLocker locker(&this->lock);
    this->data = _data;
    this->viewport()->update();
}

/**
 * @brief Draw the contents of the HexViewer widget
 * @param event
 */
void HexViewWidget::paintEvent(QPaintEvent *event) {
    QMutexLocker locker(&this->lock);

    this->update_positions();

    QPainter painter(viewport());
    const QSize area_size = viewport()->size();
    const QSize widget_size = this->get_widget_size();
    const int visible_lines = std::max(1, area_size.height() / static_cast<int>(this->charheight) - 1);
    const qsizetype total_lines = (this->data.size() + CHARACTERS_LINE - 1) /
                                 CHARACTERS_LINE;
    this->verticalScrollBar()->setPageStep(visible_lines);
    this->verticalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(total_lines) - visible_lines));
    this->horizontalScrollBar()->setPageStep(area_size.width());
    this->horizontalScrollBar()->setRange(0, std::max(0, widget_size.width() - area_size.width()));

    // grab colors
    settings.sync();
    QColor background_color = QColor(settings.value("background_color", BACKGROUND_COLOR_DEFAULT).toUInt());
    QColor address_color = QColor(settings.value("address_color", ADDRESS_COLOR_DEFAULT).toUInt());
    QColor header_color = QColor(settings.value("header_color", HEADER_COLOR_DEFAULT).toUInt());
    QColor column_color = QColor(settings.value("column_color", COLUMN_COLOR_DEFAULT).toUInt());
    QColor alt_column_color = QColor(settings.value("alt_column_color", ALT_COLUMN_COLOR_DEFAULT).toUInt());
    QColor ascii_color = QColor(settings.value("ascii_color", ASCII_COLOR_DEFAULT).toUInt());

    // set background color
    painter.fillRect(event->rect(), background_color);
    painter.translate(-this->horizontalScrollBar()->value(), 0);

//    qDebug() << "Loading " << column_color.name() << " for column color";
//    qDebug() << "Loading " << text_color.name() << " for text color";
//    qDebug() << "Loading " << header_color.name() << " for header color";

//    painter.fillRect(QRect(this->pos_addr,
//                           event->rect().top() + this->charheight,
//                           this->pos_hex - GAP_ADR_HEX + 2,
//                           widget_size.height()),
//                           address_area_color);
//    painter.fillRect(QRect(event->rect().left(),
//                           event->rect().top(),
//                           event->rect().right(),
//                           this->charheight + GAP_HEADER),
//                           address_area_color);

    const qsizetype start_idx = verticalScrollBar()->value();
    const qsizetype end_idx = std::min(total_lines, start_idx + visible_lines);

    // print header
    painter.setPen(header_color);
    painter.drawText(this->pos_addr, this->charheight, "Offset (h)");

    for(unsigned int i=0; i<CHARACTERS_LINE; i++) {
        const QString hex_string  = QString("%1").arg(i, 2, 16, QChar('0')).toUpper();
        painter.drawText(this->pos_hex + i * 3 * this->charwidth, this->charheight, hex_string);
    }

    painter.drawText(this->pos_ascii, this->charheight, "Decoded text");

    if(this->data.size() == 0) {
        return;
    }

    int ypos = static_cast<int>(this->charheight * 2);
    for(qsizetype line_idx = start_idx; line_idx < end_idx;
        ++line_idx, ypos += static_cast<int>(this->charheight)) {

        // print address
        painter.setPen(address_color);
        QString address = QString("%1").arg(line_idx * CHARACTERS_LINE, 10, 16, QChar('0'));
        painter.drawText(this->pos_addr, ypos, address);

        // print hex characters
        for(unsigned int i=0; i<CHARACTERS_LINE; i++) {
            const qsizetype data_idx = line_idx * CHARACTERS_LINE + i;
            if(data_idx >= this->data.size()) break;
            if(i % 2 == 0) {
                painter.setPen(column_color);
            } else {
                painter.setPen(alt_column_color);
            }
            const uint8_t ch = this->data[data_idx];
            const QString hex_string  = QString("%1").arg(ch, 2, 16, QChar('0')).toUpper();
            painter.drawText(this->pos_hex + i * 3 * this->charwidth, ypos, hex_string);
        }

        // print ascii characters
        painter.setPen(ascii_color);
        for(unsigned int i=0; i<CHARACTERS_LINE; i++) {
            const qsizetype data_idx = line_idx * CHARACTERS_LINE + i;
            if(data_idx >= this->data.size()) break;
            uint8_t ch = this->data[data_idx];
            if ((ch < 0x20) || (ch > 0x7e)) {
                ch = '.';
            }
            painter.drawText(this->pos_ascii + i * this->charwidth, ypos, QChar(ch));
        }
    }
}

/**
 * @brief Calculate widget size
 * @return widget size
 */
QSize HexViewWidget::get_widget_size() const {
    const unsigned int width = this->pos_ascii +
                               (CHARACTERS_LINE + 1) * this->charwidth;
    unsigned int height = this->data.size() / CHARACTERS_LINE;
    if(this->data.size() % CHARACTERS_LINE) {
        height++;
    }

    height *= this->charheight;

    return QSize(width, height);
}

/**
 * @brief Calculate positions for the columns
 */
void HexViewWidget::update_positions() {
    this->charwidth = fontMetrics().horizontalAdvance(QLatin1Char('9'));
    this->charheight = fontMetrics().height();

    this->pos_addr = 0;
    this->pos_hex = ADR_LENGTH * this->charwidth + GAP_ADR_HEX;
    this->pos_ascii = pos_hex + (CHARACTERS_LINE * 3 - 1) * this->charwidth + GAP_HEX_ASCII;
}
