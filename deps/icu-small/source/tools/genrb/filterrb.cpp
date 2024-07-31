// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <iostream>
#include <memory>
#include <stack>

#include "filterrb.h"
#include "errmsg.h"


const char* PathFilter::kEInclusionNames[] = {
    "INCLUDE",
    "PARTIAL",
    "EXCLUDE"
};


ResKeyPath::ResKeyPath() {}

ResKeyPath::ResKeyPath(const std::string& path, UErrorCode& status) {
    if (path.empty() || path[0] != '/') {
        std::cerr << "genrb error: path must start with /: " << path << std::endl;
        status = U_PARSE_ERROR;
        return;
    }
    if (path.length() == 1) {
        return;
    }
    size_t i;
    size_t j = 0;
    while (true) {
        i = j + 1;
        j = path.find('/', i);
        std::string key = path.substr(i, j - i);
        if (key.empty()) {
            std::cerr << "genrb error: empty subpaths and trailing slashes are not allowed: " << path << std::endl;
            status = U_PARSE_ERROR;
            return;
        }
        push(key);
        if (j == std::string::npos) {
            break;
        }
    }
}

void ResKeyPath::push(const std::string& key) {
    fPath.push_back(key);
}

void ResKeyPath::pop() {
    fPath.pop_back();
}

const std::list<std::string>& ResKeyPath::pieces() const {
    return fPath;
}

std::ostream& operator<<(std::ostream& out, const ResKeyPath& value) {
    if (value.pieces().empty()) {
        out << "/";
    } else for (const auto& key : value.pieces()) {
        out << "/" << key;
    }
    return out;
}


PathFilter::~PathFilter() = default;


void SimpleRuleBasedPathFilter::addRule(const std::string& ruleLine, UErrorCode& status) {
    if (ruleLine.empty()) {
        std::cerr << "genrb error: empty filter rules are not allowed" << std::endl;
        status = U_PARSE_ERROR;
        return;
    }
    bool inclusionRule = false;
    if (ruleLine[0] == '+') {
        inclusionRule = true;
    } else if (ruleLine[0] != '-') {
        std::cerr << "genrb error: rules must start with + or -: " << ruleLine << std::endl;
        status = U_PARSE_ERROR;
        return;
    }
    ResKeyPath path(ruleLine.substr(1), status);
    addRule(path, inclusionRule, status);
}

void SimpleRuleBasedPathFilter::addRule(const ResKeyPath& path, bool inclusionRule, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    fRoot.applyRule(path, path.pieces().begin(), inclusionRule, status);
}

PathFilter::EInclusion SimpleRuleBasedPathFilter::match(const ResKeyPath& path) const {
    const Tree* node = &fRoot;

    // defaultResult "bubbles up" the nearest "definite" inclusion/exclusion rule
    EInclusion defaultResult = INCLUDE;
    if (node->fIncluded != PARTIAL) {
        // rules handled here: "+/" and "-/"
        defaultResult = node->fIncluded;
    }

    // isLeaf is whether the filter tree can provide no additional information
    // even if additional subpaths are added to the given key
    bool isLeaf = false;

    for (const auto& key : path.pieces()) {
        auto child = node->fChildren.find(key);
        // Leaf case 1: input path descends outside the filter tree
        if (child == node->fChildren.end()) {
            if (node->fWildcard) {
                // A wildcard pattern is present; continue checking
                node = node->fWildcard.get();
            } else {
                isLeaf = true;
                break;
            }
        } else {
            node = &child->second;
        }
        if (node->fIncluded != PARTIAL) {
            defaultResult = node->fIncluded;
        }
    }

    // Leaf case 2: input path exactly matches a filter leaf
    if (node->isLeaf()) {
        isLeaf = true;
    }

    // Always return PARTIAL if we are not at a leaf
    if (!isLeaf) {
        return PARTIAL;
    }

    // If leaf node is PARTIAL, return the default
    if (node->fIncluded == PARTIAL) {
        return defaultResult;
    }

    return node->fIncluded;
}


SimpleRuleBasedPathFilter::Tree::Tree(const Tree& other)
        : fIncluded(other.fIncluded), fChildren(other.fChildren) {
    // Note: can't use the default copy assignment because of the std::unique_ptr
    if (other.fWildcard) {
        fWildcard = std::make_unique<Tree>(*other.fWildcard);
    }
}

bool SimpleRuleBasedPathFilter::Tree::isLeaf() const {
    return fChildren.empty() && !fWildcard;
}

void SimpleRuleBasedPathFilter::Tree::applyRule(
        const ResKeyPath& path,
        std::list<std::string>::const_iterator it,
        bool inclusionRule,
        UErrorCode& status) {

    // Base Case
    if (it == path.pieces().end()) {
        if (isVerbose() && (fIncluded != PARTIAL || !isLeaf())) {
            std::cout << "genrb info: rule on path " << path
                << " overrides previous rules" << std::endl;
        }
        fIncluded = inclusionRule ? INCLUDE : EXCLUDE;
        fChildren.clear();
        fWildcard.reset();
        return;
    }

    // Recursive Step
    const auto& key = *it;
    if (key == "*") {
        // Case 1: Wildcard
        if (!fWildcard) {
            fWildcard = std::make_unique<Tree>();
        }
        // Apply the rule to fWildcard and also to all existing children.
        it++;
        fWildcard->applyRule(path, it, inclusionRule, status);
        for (auto& child : fChildren) {
            child.second.applyRule(path, it, inclusionRule, status);
        }
        it--;

    } else {
        // Case 2: Normal Key
        auto search = fChildren.find(key);
        if (search == fChildren.end()) {
            if (fWildcard) {
                // Deep-copy the existing wildcard tree into the new key
                search = fChildren.emplace(key, Tree(*fWildcard)).first;
            } else {
                search = fChildren.emplace(key, Tree()).first;
            }
        }
        it++;
        search->second.applyRule(path, it, inclusionRule, status);
        it--;
    }
}

void SimpleRuleBasedPathFilter::Tree::print(std::ostream& out, int32_t indent) const {
    for (int32_t i=0; i<indent; i++) out << "\t";
    out << "included: " << kEInclusionNames[fIncluded] << std::endl;
    for (const auto& child : fChildren) {
        for (int32_t i=0; i<indent; i++) out << "\t";
        out << child.first << ": {" << std::endl;
        child.second.print(out, indent + 1);
        for (int32_t i=0; i<indent; i++) out << "\t";
        out << "}" << std::endl;
    }
    if (fWildcard) {
        for (int32_t i=0; i<indent; i++) out << "\t";
        out << "* {" << std::endl;
        fWildcard->print(out, indent + 1);
        for (int32_t i=0; i<indent; i++) out << "\t";
        out << "}" << std::endl;
    }
}

void SimpleRuleBasedPathFilter::print(std::ostream& out) const {
    out << "SimpleRuleBasedPathFilter {" << std::endl;
    fRoot.print(out, 1);
    out << "}" << std::endl;
}

std::ostream& operator<<(std::ostream& out, const SimpleRuleBasedPathFilter& value) {
    value.print(out);
    return out;
}
