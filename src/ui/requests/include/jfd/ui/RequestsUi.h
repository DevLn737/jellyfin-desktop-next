#pragma once

#include <jfd/seerr/RequestListModel.h>
#include <jfd/seerr/SeerrService.h>
#include <jfd/seerr/TitleDetailsModel.h>
#include <jfd/seerr/TitleListModel.h>

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <qqmlintegration.h>

namespace jfd::ui {

  class Navigator;

  // The requests screens' way into Seerr (QML: `Requests`): whether it is
  // set up and who is signed in, models for the lists, a title's page and
  // the user's own requests, and the words for statuses and outcomes. The
  // application creates the service and sets it here before the window
  // loads; without an address `configured` stays false and the sidebar
  // item and the home row are not there.
  class RequestsUi : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Requests)
    QML_SINGLETON
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY stateChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY stateChanged)
    Q_PROPERTY(QString serverHost READ serverHost NOTIFY stateChanged)
    Q_PROPERTY(bool cleartextOverInternet READ cleartextOverInternet NOTIFY stateChanged)
    Q_PROPERTY(bool mayRequest READ mayRequest NOTIFY stateChanged)

  public:
    explicit RequestsUi(QObject* parent = nullptr);

    // The application owns the service; null detaches.
    static void setService(seerr::SeerrService* service);
    static seerr::SeerrService* service() { return s_service; }
    static RequestsUi* create(QQmlEngine*, QJSEngine*);

    // Registers the routes of the screens and makes the sidebar item follow
    // the address.
    static void registerRoutes(Navigator& navigator);

    bool configured() const { return s_service != nullptr && s_service->configured(); }
    bool signedIn() const { return s_service != nullptr && s_service->signedIn(); }
    QString userName() const { return s_service != nullptr ? s_service->userName() : QString(); }
    QString serverHost() const { return s_service != nullptr ? s_service->serverHost() : QString(); }
    bool cleartextOverInternet() const {
      return s_service != nullptr && s_service->cleartextOverInternet();
    }
    bool mayRequest() const { return s_service != nullptr && s_service->mayRequest(); }

    // "popularMovies", "popularTv", "trending" or "search"; owned by `owner`.
    Q_INVOKABLE jfd::seerr::TitleListModel* createTitles(const QString& source, QObject* owner);
    Q_INVOKABLE jfd::seerr::TitleDetailsModel* createDetails(QObject* owner);
    Q_INVOKABLE jfd::seerr::RequestListModel* createMine(QObject* owner);
    // The image provider's source for a poster URL.
    Q_INVOKABLE QString image(const QUrl& url) const;

    Q_INVOKABLE void signIn(const QString& userName, const QString& password);

    // Words, already translated.
    Q_INVOKABLE QString outcomeText(int outcome) const;
    // A MediaStatus: "Available", "Requested", ...; empty for unknown.
    Q_INVOKABLE QString mediaStatusText(int status) const;
    // A RequestStatus: "Waiting for approval", "Approved", "Declined".
    Q_INVOKABLE QString requestStatusText(int status) const;
    // "check", "clock", ... names for the badge: "available", "pending",
    // "processing", "partial", "declined" or empty.
    Q_INVOKABLE QString mediaStatusKind(int status) const;
    Q_INVOKABLE QString requestStatusKind(int status) const;

    // Same words, for the settings actions.
    static QString outcomeTextFor(int outcome);

  Q_SIGNALS:
    void stateChanged();
    void signInFinished(int outcome);
    // For the shell's toast.
    void message(const QString& text, bool error);

  private:
    inline static seerr::SeerrService* s_service = nullptr;
    inline static RequestsUi* s_instance = nullptr;
  };

} // namespace jfd::ui
