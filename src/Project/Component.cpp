#include "Component.hpp"

Component::Component(Type type) : m_type(type) {}

Component::~Component() {}

void Component::build() { Log.info("Build {}", get_name()); }

void Component::clean() { Log.info("Clean {}", get_name()); }
