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

#ifndef FILEDOWNLOADER_H
#define FILEDOWNLOADER_H

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPointer>
#include <QString>
#include <QTimer>

class FileDownloader : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Construct a downloader for a single URL.
     * @param url source URL
     * @param parent parent object
     */
    explicit FileDownloader(QUrl url, QObject *parent = nullptr,
                            qint64 maximum_bytes = 1024 * 1024);

    /**
     * @brief Destroy the downloader.
     */
    virtual ~FileDownloader();

    /**
     * @brief Get the downloaded payload.
     * @return response body
     */
    QByteArray downloadedData() const;

    /**
     * @brief Check whether the download completed successfully.
     * @return true on success
     */
    bool isSuccessful() const;

    /**
     * @brief Get the last download error message.
     * @return error message
     */
    QString errorMessage() const;

public slots:
    /** Cancel the in-flight request. The downloaded signal is still emitted. */
    void cancel();

signals:
    /**
     * @brief Signal emitted when the transfer finishes.
     */
    void downloaded();

private slots:
    /**
     * @brief Handle a completed network reply.
     * @param pReply reply object
     */
    void fileDownloaded(QNetworkReply* pReply);
    void readAvailableData();

private:
    void startRequest(const QUrl& url);
    void finishFailure(const QString& message);

    QNetworkAccessManager m_WebCtrl;
    QPointer<QNetworkReply> m_Reply;
    QTimer m_Timeout;
    QByteArray m_DownloadedData;
    bool m_Success = false;
    bool m_Finished = false;
    QString m_ErrorMessage;
    int m_RedirectCount = 0;
    qint64 m_MaximumBytes;
};

#endif // FILEDOWNLOADER_H
