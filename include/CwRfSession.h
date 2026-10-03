#pragma once
#include "CwSession.h"
#include <memory>
struct Receiver;
// Independent IQ reader; does not open/tune a device or mutate the receiver.
CwRun cwReceiverSource(const std::shared_ptr<Receiver>& receiver);
