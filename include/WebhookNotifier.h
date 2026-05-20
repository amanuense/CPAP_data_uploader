#ifndef WEBHOOK_NOTIFIER_H
#define WEBHOOK_NOTIFIER_H

#include "Config.h"

class WebhookNotifier {
public:
    static void trigger(Config* config, bool sessionSucceeded,
                        int filesProcessed = 0, unsigned long bytesTransferred = 0);

private:
    WebhookNotifier() = default;
};

#endif // WEBHOOK_NOTIFIER_H
