#pragma once

struct WalletBackendCapabilities
{
    bool walletSnapshotQuery = false;
    bool rechargeMessage = false; // 113/216
    bool payOrderMessage = false; // 115/215
    bool responseCorrelation = false;
    bool idempotentMoneyMutations = false;
    bool operationResultQuery = false;

    bool canRechargeSafely() const
    {
        return rechargeMessage && responseCorrelation
               && idempotentMoneyMutations && operationResultQuery;
    }

    bool canPayOrderSafely() const
    {
        return payOrderMessage && responseCorrelation
               && idempotentMoneyMutations && operationResultQuery;
    }

    static WalletBackendCapabilities protocolV25()
    {
        WalletBackendCapabilities value;
        value.rechargeMessage = true;
        value.payOrderMessage = true;
        // 消息存在不代表具备安全重试语义；v2.5 也未冻结普通用户余额查询合同。
        return value;
    }
};
