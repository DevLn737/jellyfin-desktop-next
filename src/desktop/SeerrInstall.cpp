#include "AppServices.h"

#include <algorithm>

#include <jfd/diagnostics/Redact.h>
#include <jfd/platform/CredentialStore.h>
#include <jfd/seerr/SeerrService.h>
#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/RequestsUi.h>

#include <jfd/library/HomeSection.h>
#include <jfd/library/ItemListModel.h>
#include <jfd/seerr/TitleListModel.h>

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    const QString AddressKey = QStringLiteral("seerr/address");
    const QString SignInKey = QStringLiteral("seerr/signIn");
    const QString CheckKey = QStringLiteral("seerr/check");
    const QString SignOutKey = QStringLiteral("seerr/signOut");

    // The shell's toast; the window exists by the time a message is sent.
    void toast(QQmlEngine& engine, const QString& text, bool error) {
      auto* app = qobject_cast<QQmlApplicationEngine*>(&engine);
      if (app == nullptr || app->rootObjects().isEmpty()) return;
      QObject* shell = app->rootObjects().first()->findChild<QObject*>(QStringLiteral("shell"));
      if (shell == nullptr) return;
      QMetaObject::invokeMethod(shell, error ? "showError" : "showMessage", Q_ARG(QVariant, QVariant(text)));
    }

    // Keeps the address out of logs and reports whatever it is set to.
    void protectAddress(const seerr::SeerrService& service) {
      diagnostics::clearProtectedAddresses();
      if (service.configured()) {
        diagnostics::addProtectedAddress(service.serverUrl().toString().toStdString());
      }
    }

    // The Popular row on the home screen: the first titles of the popular
    // list, under the roles the home rows read (the ones of ItemListModel).
    // Seerr titles carry the type "SeerrTitle" so a card opens the Seerr
    // title page.
    class PopularRow : public QAbstractListModel {
    public:
      static constexpr int MaxCards = 20;

      PopularRow(seerr::SeerrService& service, QObject* parent)
          : QAbstractListModel(parent),
            m_titles(new seerr::TitleListModel(service, seerr::TitleListModel::Source::PopularMovies, this)) {
        const auto sync = [this] {
          beginResetModel();
          endResetModel();
        };
        connect(m_titles, &QAbstractItemModel::modelReset, this, sync);
        connect(m_titles, &QAbstractItemModel::rowsInserted, this, sync);
        connect(m_titles, &QAbstractItemModel::rowsRemoved, this, sync);
        connect(m_titles, &QAbstractItemModel::dataChanged, this, sync);
      }

      void reload() { m_titles->reload(); }

      int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : std::min(m_titles->count(), MaxCards);
      }

      QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= rowCount()) return {};
        const QModelIndex source = m_titles->index(index.row());
        using Src = seerr::TitleListModel;
        using Dst = library::ItemListModel;
        switch (role) {
        case Dst::IdRole: return source.data(Src::KeyRole);
        case Dst::NameRole: return source.data(Src::NameRole);
        case Dst::TypeRole: return QStringLiteral("SeerrTitle");
        case Dst::SubtitleRole: return source.data(Src::SubtitleRole);
        case Dst::ImageUrlRole: return QUrl(source.data(Src::ImageUrlRole).toString());
        case Dst::OverviewRole: return source.data(Src::OverviewRole);
        case Dst::CommunityRatingRole: return source.data(Src::RatingRole);
        default: return {};
        }
      }

      QHash<int, QByteArray> roleNames() const override { return library::ItemListModel::itemRoleNames(); }

    private:
      seerr::TitleListModel* m_titles;
    };

    const QString PopularId = QStringLiteral("seerr.popular");
  } // namespace

  bool registerSeerrSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("seerr");
    section.context = "SeerrSettings";
    section.title = QT_TRANSLATE_NOOP("SeerrSettings", "Requests (Seerr)");
    section.order = 125;

    settings::SettingDescriptor address;
    address.key = QStringLiteral("address");
    address.type = settings::SettingType::String;
    address.defaultValue = QString();
    address.label = QT_TRANSLATE_NOOP("SeerrSettings", "Seerr address");
    address.description = QT_TRANSLATE_NOOP(
        "SeerrSettings",
        "The address of your Seerr server, for example https://seerr.example.org. Empty: no Requests "
        "in the sidebar and nothing from Seerr on the home page. You sign in with your Jellyfin "
        "account; the password is not saved.");
    section.settings.append(address);

    const auto action = [&section](const char* key, const char* label) {
      settings::SettingDescriptor setting;
      setting.key = QLatin1String(key);
      setting.type = settings::SettingType::Action;
      setting.label = label;
      section.settings.append(setting);
    };
    action("signIn", QT_TRANSLATE_NOOP("SeerrSettings", "Sign in to Seerr"));
    action("check", QT_TRANSLATE_NOOP("SeerrSettings", "Check the connection"));
    action("signOut", QT_TRANSLATE_NOOP("SeerrSettings", "Sign out of Seerr"));
    return registry.registerSection(std::move(section));
  }

  void installSeerr(AppServices& services, QQmlEngine& engine) {
    // A profile of its own (the smoke test) has no credential storage.
    services.seerrCredentials = services.isolatedProfile ? platform::createUnavailableCredentialStore()
                                                         : platform::createSystemCredentialStore();
    services.seerr = std::make_unique<seerr::SeerrService>(*services.seerrCredentials);
    seerr::SeerrService& service = *services.seerr;
    settings::Settings& store = *services.settings;

    ui::RequestsUi::setService(&service);
    ui::RequestsUi::registerRoutes(*services.navigator);

    const auto apply = [&service, &store] {
      service.setAddress(store.value(AddressKey).toString());
      protectAddress(service);
    };
    apply();
    QObject::connect(&store, &settings::Settings::valueChanged, &service, [apply](const QString& key) {
      if (key == AddressKey) apply();
    });

    // The Popular row exists while an address is set and the user is signed
    // in; without either there is nothing from Seerr on the home page.
    if (services.homeSections != nullptr) {
      library::HomeSections* sections = services.homeSections;
      auto* popular = new PopularRow(service, &service);
      const auto syncRow = [&service, sections, popular] {
        if (service.configured() && service.signedIn()) {
          library::HomeSection row;
          row.id = PopularId;
          row.title = QCoreApplication::translate("SeerrSettings", "Popular in Seerr");
          row.defaultOrder = 300;
          row.model = popular;
          row.reload = [popular] { popular->reload(); };
          if (!sections->contains(PopularId)) {
            sections->add(std::move(row));
            popular->reload();
          }
        } else {
          sections->remove(PopularId);
        }
      };
      QObject::connect(&service, &seerr::SeerrService::stateChanged, &service, syncRow);
      syncRow();
    }

    ui::Navigator* navigator = services.navigator;
    QObject::connect(&store, &settings::Settings::actionTriggered, &service,
                     [&service, &engine, navigator](const QString& key) {
                       if (key == SignInKey) {
                         if (!service.configured()) {
                           toast(engine, ui::RequestsUi::outcomeTextFor(int(seerr::Outcome::NoAddress)), true);
                         } else {
                           navigator->navigate(QStringLiteral("seerr-signin"));
                         }
                       } else if (key == CheckKey) {
                         service.check();
                       } else if (key == SignOutKey) {
                         service.signOut();
                         if (service.configured()) toast(engine, QCoreApplication::translate("SeerrSettings", "Signed out of Seerr."), false);
                       }
                     });
    QObject::connect(&service, &seerr::SeerrService::checkFinished, &service,
                     [&engine](int outcome, const QString& userName) {
                       if (outcome == int(seerr::Outcome::Ok)) {
                         toast(engine, QCoreApplication::translate("SeerrSettings", "Connected to Seerr as %1.").arg(userName), false);
                       } else {
                         toast(engine, ui::RequestsUi::outcomeTextFor(outcome), true);
                       }
                     });
  }

  void uninstallSeerr(AppServices& services) {
    // The QML singleton reads the service until the engine is gone.
    ui::RequestsUi::setService(nullptr);
    services.seerr.reset();
    services.seerrCredentials.reset();
    diagnostics::clearProtectedAddresses();
  }

} // namespace jfd::desktop
