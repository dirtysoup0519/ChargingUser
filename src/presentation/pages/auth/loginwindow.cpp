#include "loginwindow.h"
#include "dragscrollhelper.h"
#include "legaldocumentpage.h"
#include "ui_loginwindow.h"
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QString feedbackKind(SubmitState state)
{
    switch (state) {
    case SubmitState::Success: return QStringLiteral("success");
    case SubmitState::ResultUnknown: return QStringLiteral("unknown");
    case SubmitState::ValidationError:
    case SubmitState::NetworkError:
    case SubmitState::ServerError: return QStringLiteral("error");
    case SubmitState::Idle:
    case SubmitState::Loading: return QStringLiteral("neutral");
    }
    return QStringLiteral("neutral");
}

void refreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

} // namespace

LoginWindow::LoginWindow(QWidget *parent) : QWidget(parent), ui(new Ui::LoginWindow)
{
    ui->setupUi(this);
    ui->brandLabel->hide();
    ui->sloganLabel->hide();
    ui->heroImage->hide();
    auto *heroBanner = new QFrame(this);
    heroBanner->setObjectName(QStringLiteral("loginHeroBanner"));
    heroBanner->setMinimumHeight(286);
    heroBanner->setMaximumHeight(286);
    heroBanner->setStyleSheet(QStringLiteral(
        "#loginHeroBanner{background:#F3F8FF;border:none;border-radius:22px;}"));
    auto *heroLayout = new QGridLayout(heroBanner);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    auto *heroBackground = new QLabel(heroBanner);
    heroBackground->setObjectName(QStringLiteral("loginHeroBackground"));
    heroBackground->setAlignment(Qt::AlignCenter);
    heroBackground->setPixmap(
        QPixmap(QStringLiteral(":/images/login_hero.png"))
            .scaled(QSize(360, 286), Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation));
    heroLayout->addWidget(heroBackground, 0, 0);
    auto *heroWords = new QWidget(heroBanner);
    heroWords->setObjectName(QStringLiteral("loginHeroWords"));
    heroWords->setAutoFillBackground(false);
    heroWords->setStyleSheet(QStringLiteral(
        "#loginHeroWords{background:transparent;border:none;}"));
    heroWords->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *wordsLayout = new QVBoxLayout(heroWords);
    wordsLayout->setContentsMargins(18, 16, 18, 16);
    wordsLayout->setSpacing(2);
    auto *brand = new QLabel(tr("智充"), heroWords);
    brand->setStyleSheet(QStringLiteral(
        "background:transparent;color:#0874F9;font-size:28px;font-weight:800;"));
    auto *slogan = new QLabel(tr("让每一次出发，都充满能量"), heroWords);
    slogan->setStyleSheet(QStringLiteral(
        "background:transparent;color:#526B8C;font-size:12px;font-weight:600;"));
    wordsLayout->addWidget(brand);
    wordsLayout->addWidget(slogan);
    wordsLayout->addStretch();
    heroLayout->addWidget(heroWords, 0, 0);
    ui->rootLayout->insertWidget(0, heroBanner);
    ui->agreementLabel->setTextFormat(Qt::RichText);
    ui->agreementLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    ui->agreementLabel->setOpenExternalLinks(false);
    ui->agreementLabel->setText(
        QStringLiteral("<a style='color:#1677FF;text-decoration:none;' href='agreement'>《用户协议》</a>"
                       "和<a style='color:#1677FF;text-decoration:none;' href='privacy'>《隐私政策》</a>"));
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setObjectName(QStringLiteral("loginPasswordEdit"));
    m_passwordEdit->setMinimumHeight(48);
    m_passwordEdit->setPlaceholderText(tr("请输入密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->hide();
    const int accountFieldIndex = ui->rootLayout->indexOf(ui->editPhoneNumber);
    ui->rootLayout->insertWidget(accountFieldIndex + 1, m_passwordEdit);
    m_loginModeButton = new QPushButton(tr("使用用户名密码登录"), this);
    m_loginModeButton->setObjectName(QStringLiteral("loginModeButton"));
    m_loginModeButton->setCursor(Qt::PointingHandCursor);
    m_loginModeButton->setStyleSheet(QStringLiteral(
        "border:none;background:transparent;color:#1677FF;font-size:12px;text-align:left;padding:7px 2px;"));
    ui->rootLayout->insertWidget(accountFieldIndex + 2, m_loginModeButton);
    // 产品界面统一使用手机号免密登录；用户名/密码入口保留接口兼容，但不呈现。
    m_loginModeButton->hide();
    m_passwordEdit->hide();
    m_usernameLogin = false;
    m_legalPage = new LegalDocumentPage(this);
    m_legalPage->setGeometry(rect());
    m_legalPage->hide();
    connect(ui->agreementLabel, &QLabel::linkActivated,
            this, &LoginWindow::openLegalDocument);
    connect(m_legalPage, &LegalDocumentPage::backRequested,
            m_legalPage, &QWidget::hide);
    connect(m_loginModeButton, &QPushButton::clicked, this, [this] {
        setUsernameLogin(!m_usernameLogin);
    });
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &LoginWindow::submitCurrentInput);
    DragScrollHelper::enableFor(this);
    connect(ui->btnLogin, &QPushButton::clicked,
            this, &LoginWindow::submitCurrentInput);
    connect(ui->editPhoneNumber, &QLineEdit::returnPressed,
            this, &LoginWindow::submitCurrentInput);
    connect(ui->agreementCheck, &QCheckBox::toggled, this, [this](bool checked) {
        ui->agreementCheck->setProperty("validationError", false);
        refreshStyle(ui->agreementCheck);
        if (checked && ui->errorLabel->property("agreementValidation").toBool()) {
            ui->errorLabel->clear();
            ui->errorLabel->hide();
            ui->errorLabel->setProperty("agreementValidation", false);
        }
    });
    render(LoginViewState{});
}

LoginWindow::~LoginWindow() { delete ui; }

void LoginWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_legalPage) m_legalPage->setGeometry(rect());
}

void LoginWindow::openLegalDocument(const QString &target)
{
    if (target == QStringLiteral("agreement")) {
        m_legalPage->showDocument(
            tr("用户协议"),
            tr("欢迎使用智充。您应使用真实、合法的手机号登录，并妥善保管账号及设备。您可以通过本应用查询充电站、预约充电桩、扫码启动充电、查看订单并使用钱包完成支付。充电、预约、取消、退款和费用结算以服务端确认的设备状态、计费规则及订单结果为准；请勿重复提交支付或恶意占用充电资源。因网络中断导致结果暂时未知时，请等待系统查询最终结果。继续使用本应用即表示您同意遵守适用法律法规、站点现场规则及本协议。"));
    } else {
        m_legalPage->showDocument(
            tr("隐私政策"),
            tr("智充仅在提供登录、站点查询、预约、扫码充电、订单结算和钱包服务所必需的范围内处理您的手机号、账号资料、位置、充电桩标识、订单及支付记录。位置信息用于展示附近站点和规划路线，摄像头仅在您主动扫码时使用；未经授权不会用于其他目的。我们采取合理措施保护数据安全，不在客户端保存支付密钥或完整敏感凭据，并按照法律要求和业务所需期限保存相关记录。您可以停止位置或摄像头权限，并通过账号相关入口查询或修改个人资料。"));
    }
    m_legalPage->setGeometry(rect());
    m_legalPage->show();
    m_legalPage->raise();
}

void LoginWindow::setUsernameLogin(bool enabled)
{
    m_usernameLogin = enabled;
    ui->editPhoneNumber->clear();
    ui->editPhoneNumber->setMaxLength(enabled ? 32 : 11);
    ui->editPhoneNumber->setPlaceholderText(
        enabled ? tr("请输入用户名") : tr("请输入手机号码"));
    m_passwordEdit->clear();
    m_passwordEdit->setVisible(enabled);
    m_loginModeButton->setText(
        enabled ? tr("使用手机号登录") : tr("使用用户名密码登录"));
    ui->hintLabel->setText(enabled
                               ? tr("输入用户名和密码登录")
                               : tr("输入手机号，开始寻找附近充电站"));
}

void LoginWindow::render(const LoginViewState &state)
{
    const bool explicitlyCleared = !state.phoneInput.isNull()
                                   && state.phoneInput.isEmpty();
    if (!state.phoneInput.isNull()
        && (explicitlyCleared || !ui->editPhoneNumber->hasFocus())
        && ui->editPhoneNumber->text() != state.phoneInput) {
        const QSignalBlocker blocker(ui->editPhoneNumber);
        ui->editPhoneNumber->setText(state.phoneInput);
    }

    const bool loading = state.submitState == SubmitState::Loading;
    ui->btnLogin->setEnabled(state.canSubmit && !loading);
    ui->btnLogin->setText(loading ? tr("登录中…") : tr("登录"));
    ui->loadingIndicator->setVisible(loading);

    QString message = state.message;
    if (state.submitState == SubmitState::ResultUnknown && message.isEmpty())
        message = tr("登录结果暂时未知，请稍后重试。");
    ui->errorLabel->setText(message);
    ui->errorLabel->setVisible(!message.isEmpty());
    ui->errorLabel->setProperty("agreementValidation", false);
    ui->errorLabel->setProperty("feedbackKind", feedbackKind(state.submitState));
    refreshStyle(ui->errorLabel);
}

void LoginWindow::submitCurrentInput()
{
    if (!ui->btnLogin->isEnabled())
        return;
    if (!ui->agreementCheck->isChecked()) {
        ui->agreementCheck->setProperty("validationError", true);
        refreshStyle(ui->agreementCheck);
        ui->errorLabel->setText(tr("请先阅读并同意《用户协议》和《隐私政策》"));
        ui->errorLabel->setProperty("agreementValidation", true);
        ui->errorLabel->setProperty("feedbackKind", QStringLiteral("error"));
        ui->errorLabel->show();
        refreshStyle(ui->errorLabel);
        return;
    }
    const QString account = ui->editPhoneNumber->text().trimmed();
    emit loginRequested(account);
}
