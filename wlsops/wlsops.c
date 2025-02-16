#include "wlsops.h"
#include "WLSINC.h"
#include <linux/string.h>
#include <net/net_namespace.h>

_adapter *padapter = NULL;
struct ieee80211_local *wls_local = NULL;
struct ieee80211_vif *wls_vif = NULL;
struct ieee80211_vif *t_wls_vif = NULL;
struct ieee80211_hw *wls_hw = NULL;

// create a pointer array
static int max_adapters = 4;
module_param(max_adapters, int, 0644);
MODULE_PARM_DESC(max_adapters, "Maximum number of adapters");

struct uint32_t **wls_adapter = NULL;
uint8_t *adapter_type = NULL;

const char* substring = "wlx";

int wls_hack_init(void)
{
    struct net_device *dev;
    struct net * net;
    struct rtw_netdev_priv_indicator *ptr;
    int counter = 0;

    wls_adapter = kmalloc_array(max_adapters, sizeof(struct uint32_t *), GFP_KERNEL);
    if (!wls_adapter) {
        printh("Failed to allocate memory for wls_adapter\n");
        return -ENOMEM;
    }

    uint8_t i;
    for (i = 0; i < max_adapters; i++) {
        wls_adapter[i] = NULL;
    }


    // Create u8 array to indicate the type of adapter, 0 not found, 1 intel, 2 realtek
    adapter_type = kmalloc_array(max_adapters, sizeof(uint8_t), GFP_KERNEL);
    if (!adapter_type) {
        printh("Failed to allocate memory for adapter_type\n");
        kfree(wls_adapter);
        return -ENOMEM;
    }

    for_each_net(net){
        for_each_netdev(net, dev)
        {
            if (strstr(dev->name, substring) != NULL)
            {
                ptr = ((struct rtw_netdev_priv_indicator *)netdev_priv(dev));
                // printh( "private address: %p, %d\n", ptr->priv, ptr->sizeof_priv );
                padapter = (_adapter *)rtw_netdev_priv(dev);
                if (padapter) {
                    // printh("adapter: %p\n", padapter);
                    wls_adapter[counter] = (struct uint32_t *)padapter;
                    adapter_type[counter] = 2;
                    printh("find wireless realtek device: %s, with index %d\n", dev->name, counter);
                    counter += 1;
                }
            }
            if( dev->ieee80211_ptr ) // is a 802.11 device
            {
                t_wls_vif = wdev_to_ieee80211_vif(dev->ieee80211_ptr);
                if (t_wls_vif) {
                    wls_local = (struct ieee80211_local *) wdev_priv(dev->ieee80211_ptr);
                    wls_adapter[counter] = (struct uint32_t *)wls_local;
                    adapter_type[counter] = 1;
                    wls_hw = &wls_local->hw;
                    wls_vif = t_wls_vif;
                    printh("find 802.11 device: %s, with index %d\n", dev->name, counter);
                    counter += 1;
                }
            }
            if (counter == max_adapters) break;
        }
    }

    printh("Complete init.\n");
    return 0;
}

int wls_conf_tx(struct tx_param param)
{
    if (wls_adapter[param.if_ind]){
        if (param.if_ind >= max_adapters){
            printh("Invalid adapter index\n");
            return -1;
        }
        else if (adapter_type[param.if_ind] == 1){
            return intel_conf_tx(param);
        }
        else if (adapter_type[param.if_ind] == 2){
            return realtek_conf_tx(param);
        }
        else if (adapter_type[param.if_ind] == 0){
            printh("Adapter with ind %d not found\n", param.if_ind);
            return -1;
        }
    }

    return 0;
}

int intel_conf_tx(struct tx_param param){
    printh("Modify Intel IC\n");                                               
    int ret;
    struct ieee80211_tx_queue_params wls_params = 
    {
        .txop = param.txop,
        .cw_min = param.cw_min,
        .cw_max = param.cw_max,
        .aifs = param.aifs,
        // .acm = false,
        // .uapsd = false,
        // .mu_edca = false
    };
    // printh("wls_vif: %p, wls_local: %p, wls_hw:%p\n", wls_vif, wls_local, wls_hw);
    ret = wls_local->ops->conf_tx(wls_hw, wls_vif, param.ac, &wls_params);
    return ret;
}


int realtek_conf_tx(struct tx_param param){
    printh("Modify Realtek IC\n");
        
    uint8_t CWMIN, CWMAX, AIFS, aSifsTime;
    uint16_t TXOP;
    uint32_t acParm;

    // Get adapter
    _adapter * _padapter = (_adapter *)wls_adapter[param.if_ind];

    struct mlme_ext_priv	*pmlmeext = &_padapter->mlmeextpriv;
    struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

    // if (IsSupported5G(pmlmeext->cur_wireless_mode) || 
    // 	(pmlmeext->cur_wireless_mode & WIRELESS_11_24N) )
    // 	aSifsTime = 16;
    // else
    aSifsTime = 10;
    CWMIN = (uint8_t)param.cw_min ;
    CWMAX = (uint8_t)param.cw_max ;
    AIFS = param.aifs * pmlmeinfo->slotTime + aSifsTime;
    TXOP = param.txop;
    printh("CW-min %d, CW-max %d, AIFS (ms) %d,  TXOP %d, Adapter: %p\n", CWMIN,CWMAX,AIFS,TXOP,_padapter);
    acParm = AIFS | (CWMIN << 8) | (CWMAX << 12) | (TXOP << 16);
    // printh("AC_param_value %d\n", acParm);

    switch (param.ac)
    {
    case 0:
        _padapter->hal_func.set_hw_reg_handler(_padapter,HW_VAR_AC_PARAM_VO, (uint8_t*)(&acParm));
        break;
    case 1:
        _padapter->hal_func.set_hw_reg_handler(_padapter,HW_VAR_AC_PARAM_VI, (uint8_t*)(&acParm));
        break;
    case 2:
        _padapter->hal_func.set_hw_reg_handler(_padapter,HW_VAR_AC_PARAM_BE, (uint8_t*)(&acParm));
        
        break;
    case 3: 
        _padapter->hal_func.set_hw_reg_handler(_padapter,HW_VAR_AC_PARAM_BK, (uint8_t*)(&acParm));
        break;
    default:
        _padapter->hal_func.set_hw_reg_handler(_padapter,HW_VAR_AC_PARAM_BE, (uint8_t*)(&acParm));
        break;
    }
    return 0; 
}
