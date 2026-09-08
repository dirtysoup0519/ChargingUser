#pragma once

/** 服务端协议能力必须显式声明；不能从消息码存在推断安全语义。 */
struct ChargingBackendCapabilities
{
    bool combinedStationChargerQuery = false; // v2.5: 119/229
    bool stableStationAndChargerIds = false;
    bool responseCorrelation = false;
    bool idempotentChargingMutations = false;
    bool operationResultQuery = false;

    bool canLoadAuthoritativeConfirmation() const
    {
        return combinedStationChargerQuery && stableStationAndChargerIds;
    }

    bool canStartChargingSafely() const
    {
        return canLoadAuthoritativeConfirmation() && responseCorrelation
               && idempotentChargingMutations && operationResultQuery;
    }

    /** 仓库记录的 v2.5 实际能力；108/208 存在不等于支持安全变更。 */
    static ChargingBackendCapabilities protocolV25()
    {
        ChargingBackendCapabilities value;
        value.combinedStationChargerQuery = true;
        return value;
    }
};
