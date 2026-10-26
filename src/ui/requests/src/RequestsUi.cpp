#include "jfd/ui/RequestsUi.h"

#include "jfd/ui/LibraryImageProvider.h"
#include "jfd/ui/Navigator.h"

#include <QCoreApplication>
#include <QPointer>

namespace jfd::ui {

  using seerr::MediaStatus;
  using seerr::Outcome;
  using seerr::RequestStatus;
  using seerr::TitleListModel;


  RequestsUi::RequestsUi(QObject* parent) : QObject(parent) {
    s_instance = this;
    if (s_service != nullptr) {
      connect(s_service, &seerr::SeerrService::stateChanged, this, &RequestsUi::stateChanged);
      connect(s_service, &seerr::SeerrService::signInFinished, this, &RequestsUi::signInFinished);
    }
  }

  RequestsUi::~RequestsUi() {
    // The engine deletes the singleton; a later setService must not reach it.
    if (s_instance == this) s_instance = nullptr;
  }

  void RequestsUi::setService(seerr::SeerrService* service) {
    s_service = service;
    if (s_instance != nullptr && service != nullptr) {
      connect(service, &seerr::SeerrService::stateChanged, s_instance, &RequestsUi::stateChanged,
              Qt::UniqueConnection);
      connect(service, &seerr::SeerrService::signInFinished, s_instance, &RequestsUi::signInFinished,
              Qt::UniqueConnection);
      Q_EMIT s_instance->stateChanged();
    }
  }

  RequestsUi* RequestsUi::create(QQmlEngine*, QJSEngine*) {
    // The engine owns the singleton it creates; it lives as long as the QML.
    auto* ui = new RequestsUi();
    return ui;
  }

  void RequestsUi::registerRoutes(Navigator& navigator) {
    const QString base = QStringLiteral("qrc:/qt/qml/Jfd/Ui/Requests/");
    RouteSpec browse;
    browse.name = QStringLiteral("requests");
    browse.page = QUrl(base + QStringLiteral("BrowsePage.qml"));
    browse.hasSidebarEntry = true;
    browse.sidebar.icon = base + QStringLiteral("icons/requests.svg");
    browse.sidebar.text = QString::fromUtf8(QT_TRANSLATE_NOOP("Sidebar", "Requests"));
    browse.sidebar.translationContext = "Sidebar";
    browse.sidebar.order = 250;
    navigator.registerRoute(browse);

    RouteSpec title;
    title.name = QStringLiteral("seerr-title");
    title.takesArgument = true;
    title.page = QUrl(base + QStringLiteral("TitlePage.qml"));
    navigator.registerRoute(title);

    RouteSpec mine;
    mine.name = QStringLiteral("requests-mine");
    mine.page = QUrl(base + QStringLiteral("MyRequestsPage.qml"));
    navigator.registerRoute(mine);

    RouteSpec signIn;
    signIn.name = QStringLiteral("seerr-signin");
    signIn.page = QUrl(base + QStringLiteral("SignInPage.qml"));
    navigator.registerRoute(signIn);

    // The sidebar item exists only while an address is set.
    const auto follow = [&navigator] {
      navigator.setRouteAvailable(QStringLiteral("requests"), RequestsUi::s_service != nullptr &&
                                                                 RequestsUi::s_service->configured());
    };
    follow();
    if (s_service != nullptr) {
      QObject::connect(s_service, &seerr::SeerrService::stateChanged, &navigator, follow);
    }
  }

  jfd::seerr::TitleListModel* RequestsUi::createTitles(const QString& source, QObject* owner) {
    if (s_service == nullptr) return nullptr;
    TitleListModel::Source kind = TitleListModel::Source::Search;
    if (source == QLatin1String("popularMovies")) kind = TitleListModel::Source::PopularMovies;
    else if (source == QLatin1String("popularTv")) kind = TitleListModel::Source::PopularTv;
    else if (source == QLatin1String("trending")) kind = TitleListModel::Source::Trending;
    return new TitleListModel(*s_service, kind, owner);
  }

  jfd::seerr::TitleDetailsModel* RequestsUi::createDetails(QObject* owner) {
    return s_service != nullptr ? new seerr::TitleDetailsModel(*s_service, owner) : nullptr;
  }

  jfd::seerr::RequestListModel* RequestsUi::createMine(QObject* owner) {
    return s_service != nullptr ? new seerr::RequestListModel(*s_service, owner) : nullptr;
  }

  QString RequestsUi::image(const QUrl& url) const { return LibraryImageProvider::sourceFor(url); }

  void RequestsUi::signIn(const QString& userName, const QString& password) {
    if (s_service != nullptr) s_service->signIn(userName, password);
  }

  QString RequestsUi::outcomeText(int outcome) const { return outcomeTextFor(outcome); }

  QString RequestsUi::outcomeTextFor(int outcome) {
    switch (Outcome(outcome)) {
    case Outcome::Ok: return QCoreApplication::translate("RequestsUi", "Done.");
    case Outcome::NoAddress: return QCoreApplication::translate("RequestsUi", "Set the Seerr address in Settings first.");
    case Outcome::Unreachable: return QCoreApplication::translate("RequestsUi", "Cannot reach Seerr. Check the address and the network.");
    case Outcome::NotJellyfin:
      return QCoreApplication::translate("RequestsUi", "This server is not a Seerr that uses Jellyfin to sign in. Nothing was sent.");
    case Outcome::SignedOut: return QCoreApplication::translate("RequestsUi", "Not signed in to Seerr, or the session ran out. Sign in again.");
    case Outcome::BadLogin: return QCoreApplication::translate("RequestsUi", "Seerr refused the name or password.");
    case Outcome::NoPermission:
      return QCoreApplication::translate("RequestsUi", "Your Seerr account may not do this, or its request limit is used up.");
    case Outcome::AlreadyDone: return QCoreApplication::translate("RequestsUi", "It is already requested or available.");
    case Outcome::ServerError: return QCoreApplication::translate("RequestsUi", "Seerr had an error. Try again later.");
    case Outcome::Other: break;
    }
    return QCoreApplication::translate("RequestsUi", "Seerr did not accept this.");
  }

  QString RequestsUi::mediaStatusText(int status) const {
    switch (MediaStatus(status)) {
    case MediaStatus::Pending: return QCoreApplication::translate("RequestsUi", "Requested");
    case MediaStatus::Processing: return QCoreApplication::translate("RequestsUi", "Being added");
    case MediaStatus::PartiallyAvailable: return QCoreApplication::translate("RequestsUi", "Partly available");
    case MediaStatus::Available: return QCoreApplication::translate("RequestsUi", "Available");
    case MediaStatus::None:
    case MediaStatus::Unknown:
    case MediaStatus::Deleted: break;
    }
    return {};
  }

  QString RequestsUi::requestStatusText(int status) const {
    switch (RequestStatus(status)) {
    case RequestStatus::PendingApproval: return QCoreApplication::translate("RequestsUi", "Waiting for approval");
    case RequestStatus::Approved: return QCoreApplication::translate("RequestsUi", "Approved");
    case RequestStatus::Declined: return QCoreApplication::translate("RequestsUi", "Declined");
    case RequestStatus::None: break;
    }
    return {};
  }

  QString RequestsUi::mediaStatusKind(int status) const {
    switch (MediaStatus(status)) {
    case MediaStatus::Pending: return QStringLiteral("pending");
    case MediaStatus::Processing: return QStringLiteral("processing");
    case MediaStatus::PartiallyAvailable: return QStringLiteral("partial");
    case MediaStatus::Available: return QStringLiteral("available");
    default: return {};
    }
  }

  QString RequestsUi::requestStatusKind(int status) const {
    switch (RequestStatus(status)) {
    case RequestStatus::PendingApproval: return QStringLiteral("pending");
    case RequestStatus::Approved: return QStringLiteral("processing");
    case RequestStatus::Declined: return QStringLiteral("declined");
    default: return {};
    }
  }

} // namespace jfd::ui
