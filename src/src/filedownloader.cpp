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

#include "filedownloader.h"

#include <QNetworkRequest>
#include <QTimer>

/**
 * @brief Construct a downloader for a single URL.
 * @param url source URL
 * @param parent parent object
 */
FileDownloader::FileDownloader(QUrl url, QObject *parent, qint64 maximum_bytes) :
    QObject(parent),
    m_MaximumBytes(maximum_bytes) {

    // add connection
    connect(
        &m_WebCtrl, SIGNAL (finished(QNetworkReply*)),
        this, SLOT (fileDownloaded(QNetworkReply*))
    );

    m_Timeout.setSingleShot(true);
    connect(&m_Timeout, &QTimer::timeout, this, [this]() {
        if(m_Finished) return;
        m_ErrorMessage = tr("Download timed out after 30 seconds.");
        if(m_Reply) m_Reply->abort();
        else finishFailure(m_ErrorMessage);
    });

    QTimer::singleShot(0, this, [this, url]() { startRequest(url); });
}

/**
 * @brief Destroy the downloader.
 */
FileDownloader::~FileDownloader() { }

void FileDownloader::cancel()
{
    if(m_Finished) return;
    m_ErrorMessage = tr("Download cancelled.");
    if(m_Reply) m_Reply->abort();
    else finishFailure(m_ErrorMessage);
}

/**
 * @brief Handle a completed network reply.
 * @param pReply reply object
 */
void FileDownloader::startRequest(const QUrl& url)
{
    if(m_Finished) return;
    if(!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        finishFailure(tr("Only valid HTTPS download URLs are accepted."));
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    m_Reply = m_WebCtrl.get(request);
    connect(m_Reply, &QNetworkReply::readyRead, this, &FileDownloader::readAvailableData);
    connect(m_Reply, &QNetworkReply::metaDataChanged, this, [this]() {
        if(!m_Reply || m_Finished) return;
        const qint64 length = m_Reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if(length > m_MaximumBytes) {
            m_ErrorMessage = tr("Download is too large (%1 bytes; limit is %2 bytes).")
                .arg(length).arg(m_MaximumBytes);
            m_Reply->abort();
        }
    });
    if(!m_Timeout.isActive()) m_Timeout.start(30000);
}

void FileDownloader::readAvailableData()
{
    if(!m_Reply || m_Finished) return;
    m_DownloadedData.append(m_Reply->readAll());
    if(m_DownloadedData.size() > m_MaximumBytes) {
        m_ErrorMessage = tr("Download exceeded the %1-byte limit.").arg(m_MaximumBytes);
        m_Reply->abort();
    }
}

void FileDownloader::fileDownloaded(QNetworkReply* pReply) {

    if(m_Finished || pReply != m_Reply) {
        pReply->deleteLater();
        return;
    }
    readAvailableData();
    if(pReply->error() != QNetworkReply::NoError) {
        const QString error = m_ErrorMessage.isEmpty() ? pReply->errorString() : m_ErrorMessage;
        pReply->deleteLater();
        m_Reply.clear();
        finishFailure(error);
        return;
    }

    int statuscode = pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "HttpStatusCode: " << statuscode;

    QVariant redirectTarget = pReply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (statuscode >= 300 && statuscode < 400 && redirectTarget.isValid()) {
        m_RedirectCount++;
        if(m_RedirectCount > 5) {
            pReply->deleteLater();
            m_Reply.clear();
            finishFailure(tr("Too many HTTP redirects."));
            return;
        }
        QUrl redirectUrl = pReply->url().resolved(redirectTarget.toUrl());
        pReply->deleteLater();
        m_Reply.clear();
        m_DownloadedData.clear();
        startRequest(redirectUrl);
        return;
    }

    if(statuscode < 200 || statuscode >= 300) {
        pReply->deleteLater();
        m_Reply.clear();
        finishFailure(tr("Unexpected HTTP status code %1.").arg(statuscode));
        return;
    }

    if(m_DownloadedData.isEmpty()) {
        pReply->deleteLater();
        m_Reply.clear();
        finishFailure(tr("The server returned an empty response."));
        return;
    }
    m_Success = true;
    m_Finished = true;
    m_Timeout.stop();

    pReply->deleteLater();
    m_Reply.clear();
    emit downloaded();
}

void FileDownloader::finishFailure(const QString& message)
{
    if(m_Finished) return;
    m_Timeout.stop();
    m_Finished = true;
    m_Success = false;
    m_ErrorMessage = message;
    emit downloaded();
}

/**
 * @brief Get the downloaded payload.
 * @return response body
 */
QByteArray FileDownloader::downloadedData() const {
    return m_DownloadedData;
}

/**
 * @brief Check whether the download completed successfully.
 * @return true on success
 */
bool FileDownloader::isSuccessful() const {
    return m_Success;
}

/**
 * @brief Get the last download error message.
 * @return error message
 */
QString FileDownloader::errorMessage() const {
    return m_ErrorMessage;
}
