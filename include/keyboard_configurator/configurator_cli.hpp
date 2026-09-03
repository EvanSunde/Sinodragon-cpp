#pragma once

namespace kb::cfg {

class Runtime;

// Interactive stdin frontend. All it does is read lines and hand them to the
// runtime's dispatcher -- the same dispatcher the control socket uses -- so
// the two frontends can never drift apart.
class ConfiguratorCLI {
public:
    explicit ConfiguratorCLI(Runtime& runtime);

    void run();

private:
    void printBanner() const;

    Runtime& runtime_;
};

}  // namespace kb::cfg
