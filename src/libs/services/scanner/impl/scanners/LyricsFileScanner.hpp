/*
 * Copyright (C) 2024 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "IFileScanner.hpp"

namespace lms
{
    namespace db
    {
        class Db;
    }
} // namespace lms

namespace lms::scanner
{
    struct ScannerSettings;

    class LyricsFileScanner : public IFileScanner
    {
    public:
        LyricsFileScanner(db::Db& db);
        ~LyricsFileScanner() override = default;
        LyricsFileScanner(const LyricsFileScanner&) = delete;
        LyricsFileScanner& operator=(const LyricsFileScanner&) = delete;

    private:
        core::LiteralString getName() const override;
        std::span<const std::filesystem::path> getSupportedExtensions() const override;
        bool needsScan(ScanContext& context, const FileToScan& file) const override;
        std::unique_ptr<IFileScanOperation> createScanOperation(const FileToScan& fileToScan) const override;

        db::Db& _db;
    };
} // namespace lms::scanner
