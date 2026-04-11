#include "commands/RepCmd.h"
#include "disk/BinaryIO.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/FsFileOps.h"
#include "structs/MBR.h"
#include "structs/EBR.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <functional>

static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static inline std::string tolower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static inline std::string strip_quotes(std::string v) {
    v = trim(v);
    if (v.size() >= 2) {
        if ((v.front()=='"' && v.back()=='"') || (v.front()=='\'' && v.back()=='\'')) {
            return v.substr(1, v.size()-2);
        }
    }
    return v;
}

static std::unordered_map<std::string,std::string> parse_params_local(const std::string& line) {
    std::unordered_map<std::string,std::string> p;
    std::istringstream iss(line);
    std::string token;
    iss >> token; // comando

    while (iss >> token) {
        if (token.rfind("-", 0) != 0) continue;

        auto eq = token.find('=');
        if (eq == std::string::npos) {
            p[tolower_str(token.substr(1))] = "true";
        } else {
            std::string key = tolower_str(token.substr(1, eq-1));
            std::string val = strip_quotes(token.substr(eq+1));
            p[key] = val;
        }
    }
    return p;
}

static std::string part_name_to_string(const char part_name[16]) {
    char tmp[17];
    std::memset(tmp, 0, sizeof(tmp));
    std::memcpy(tmp, part_name, 16);
    return std::string(tmp);
}

static bool is_used_part(const Partition& p) {
    return p.part_status == '1' && p.part_start >= 0 && p.part_size > 0;
}

static bool find_extended_part(const MBR& mbr, Partition& out) {
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (is_used_part(p) && (p.part_type == 'E' || p.part_type == 'e')) {
            out = p;
            return true;
        }
    }
    return false;
}

std::string RepCmd::exec(const std::string& line) {
    auto params = parse_params_local(line);

    if (!params.count("name")) return "ERROR: rep -> falta -name\n";
    if (!params.count("path")) return "ERROR: rep -> falta -path (salida)\n";

    std::string name = tolower_str(params["name"]);
    std::string out_path = params["path"];

    // ===== Fuente: preferir -id (calificación), si no usar -disk (debug) =====
    std::string disk_path;
    std::string used_id;

    if (params.count("id")) {
        used_id = params["id"];
        MountEntry me{};
        if (!MountManager::instance().getById(used_id, me)) {
            return "ERROR: rep -> id no encontrado (use mounted para ver ids)\n";
        }
        disk_path = me.path;
    } else if (params.count("disk")) {
        disk_path = params["disk"];
    } else {
        return "ERROR: rep -> falta -id (partición montada) o -disk (disco fuente)\n";
    }

    // Crear directorios de salida si no existen
    std::filesystem::path p(out_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream out(out_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return "ERROR: rep -> no se pudo crear el archivo de reporte\n";

    auto finish_ok = [&](const std::string& kind) {
        out.close();
        std::ostringstream msg;
        msg << "OK: rep -> " << kind << " generado en: " << out_path << "\n";
        return msg.str();
    };

    // Leer MBR del disco
    MBR mbr{};
    if (!BinaryIO::readAt0(disk_path, &mbr, sizeof(MBR))) {
        return "ERROR: rep -> no se pudo leer el MBR del disco\n";
    }

    if (name == "mbr") {
        out << "REPORTE MBR\n";
        out << "DISK: " << disk_path << "\n";
        if (!used_id.empty()) out << "ID: " << used_id << "\n";
        out << "\n";

        out << "mbr_tamano: " << mbr.mbr_tamano << " bytes\n";
        out << "mbr_fecha_creacion: " << mbr.mbr_fecha_creacion << "\n";
        out << "mbr_disk_signature: " << mbr.mbr_disk_signature << "\n";
        out << "mbr_fit: " << mbr.mbr_fit << "\n\n";

        out << "PARTICIONES (4)\n";
        out << "idx | status | type | fit | start | size | name\n";
        out << "----+--------+------+-----+-------+------+----------------\n";

        for (int i = 0; i < 4; i++) {
            const auto& pt = mbr.mbr_partitions[i];
            out << i
                << "   | " << pt.part_status
                << "      | " << pt.part_type
                << "    | " << pt.part_fit
                << "   | " << pt.part_start
                << "   | " << pt.part_size
                << " | " << part_name_to_string(pt.part_name)
                << "\n";
        }

        return finish_ok("mbr");
    }

    if (name == "disk") {
        out << "REPORTE DISK (MINIMO)\n";
        out << "DISK: " << disk_path << "\n";
        if (!used_id.empty()) out << "ID: " << used_id << "\n";
        out << "\n";

        out << "mbr_tamano: " << mbr.mbr_tamano << " bytes\n";
        out << "mbr_fit: " << mbr.mbr_fit << "\n\n";

        out << "PARTICIONES MBR (4)\n";
        out << "idx | status | type | fit | start | size | name\n";
        out << "----+--------+------+-----+-------+------+----------------\n";
        for (int i = 0; i < 4; i++) {
            const auto& pt = mbr.mbr_partitions[i];
            out << i
                << "   | " << pt.part_status
                << "      | " << pt.part_type
                << "    | " << pt.part_fit
                << "   | " << pt.part_start
                << "   | " << pt.part_size
                << " | " << part_name_to_string(pt.part_name)
                << "\n";
        }

        Partition ext{};
        if (!find_extended_part(mbr, ext)) {
            out << "\n(No hay partición extendida)\n";
            return finish_ok("disk");
        }

        int32_t ext_start = ext.part_start;
        int32_t ext_end   = ext.part_start + ext.part_size;

        out << "\nEXTENDIDA\n";
        out << "name: " << part_name_to_string(ext.part_name) << "\n";
        out << "start: " << ext_start << "\n";
        out << "size: " << ext.part_size << "\n";
        out << "end: " << ext_end << "\n\n";

        out << "LOGICAS (EBR CHAIN)\n";
        out << "ebr_off | status | fit | data_start | data_size | next | name\n";
        out << "--------+--------+-----+-----------+----------+------+----------------\n";

        int32_t off = ext_start;
        int guard = 0;

        while (off >= ext_start && off < ext_end && guard < 1000) {
            guard++;

            EBR e{};
            if (!BinaryIO::readAt(disk_path, off, &e, sizeof(EBR))) {
                out << "ERROR leyendo EBR en offset " << off << "\n";
                break;
            }

            out << off
                << " | " << e.part_status
                << "      | " << e.part_fit
                << "   | " << e.part_start
                << "      | " << e.part_size
                << " | " << e.part_next
                << " | " << part_name_to_string(e.part_name)
                << "\n";

            if (e.part_next == -1) break;
            off = e.part_next;
        }

        if (guard >= 1000) {
            out << "ERROR: cadena EBR demasiado larga (posible loop)\n";
        }

        return finish_ok("disk");
    }

    MountEntry mounted{};
    if (!params.count("id") || !MountManager::instance().getById(params["id"], mounted)) {
        return "ERROR: rep -> id no encontrado (use mounted para ver ids)\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (sb.s_magic != 0xEF53) return "ERROR: rep -> la partición no está formateada como EXT2/EXT3\n";

    auto dump_inode = [&](int idx) {
        Inode inode = fs.readInode(sb, idx);
        out << "inode[" << idx << "]"
            << " uid=" << inode.i_uid
            << " gid=" << inode.i_gid
            << " size=" << inode.i_size
            << " type=" << inode.i_type
            << " perm=" << inode.i_perm
            << " blocks=";
        for (int i = 0; i < 15; ++i) {
            if (inode.i_block[i] >= 0) out << inode.i_block[i] << " ";
        }
        out << "\n";
    };

    if (name == "sb") {
        out << "SUPERBLOCK\n";
        out << "s_filesystem_type: " << sb.s_filesystem_type << "\n";
        out << "s_inodes_count: " << sb.s_inodes_count << "\n";
        out << "s_blocks_count: " << sb.s_blocks_count << "\n";
        out << "s_free_blocks_count: " << sb.s_free_blocks_count << "\n";
        out << "s_free_inodes_count: " << sb.s_free_inodes_count << "\n";
        out << "s_mtime: " << sb.s_mtime << "\n";
        out << "s_umtime: " << sb.s_umtime << "\n";
        out << "s_mnt_count: " << sb.s_mnt_count << "\n";
        out << "s_magic: " << sb.s_magic << "\n";
        out << "s_inode_size: " << sb.s_inode_size << "\n";
        out << "s_block_size: " << sb.s_block_size << "\n";
        out << "s_first_ino: " << sb.s_first_ino << "\n";
        out << "s_first_blo: " << sb.s_first_blo << "\n";
        out << "s_bm_inode_start: " << sb.s_bm_inode_start << "\n";
        out << "s_bm_block_start: " << sb.s_bm_block_start << "\n";
        out << "s_inode_start: " << sb.s_inode_start << "\n";
        out << "s_block_start: " << sb.s_block_start << "\n";
        return finish_ok("sb");
    }

    if (name == "bm_inode" || name == "bm_block") {
        int count = (name == "bm_inode") ? sb.s_inodes_count : sb.s_blocks_count;
        int64_t start = (name == "bm_inode") ? sb.s_bm_inode_start : sb.s_bm_block_start;
        for (int i = 0; i < count; ++i) {
            char v = '\0';
            BinaryIO::readAt(mounted.path, start + i, &v, 1);
            out << v;
            if ((i + 1) % 32 == 0) out << "\n";
            else out << " ";
        }
        out << "\n";
        return finish_ok(name);
    }

    if (name == "inode") {
        for (int i = 0; i < sb.s_inodes_count; ++i) {
            char v = '\0';
            BinaryIO::readAt(mounted.path, sb.s_bm_inode_start + i, &v, 1);
            if (v == '1') dump_inode(i);
        }
        return finish_ok("inode");
    }

    if (name == "block") {
        for (int i = 0; i < sb.s_blocks_count; ++i) {
            char v = '\0';
            BinaryIO::readAt(mounted.path, sb.s_bm_block_start + i, &v, 1);
            if (v != '1') continue;

            FileBlock fb = fs.readFileBlock(sb, i);
            out << "block[" << i << "] raw=\"";
            for (char c : fb.b_content) {
                if (c == '\0') break;
                out << c;
            }
            out << "\"\n";
        }
        return finish_ok("block");
    }

    if (name == "file") {
        if (!params.count("path_file_ls")) return "ERROR: rep -> falta -path_file_ls\n";
        std::string content;
        if (!FsFileOps::readFile(fs, sb, params["path_file_ls"], content, nullptr)) {
            return "ERROR: rep -> no se pudo leer el archivo indicado\n";
        }
        out << content << "\n";
        return finish_ok("file");
    }

    if (name == "ls") {
        if (!params.count("path_file_ls")) return "ERROR: rep -> falta -path_file_ls\n";
        int32_t dirIdx = fs.resolvePath(sb, params["path_file_ls"]);
        if (dirIdx < 0) return "ERROR: rep -> no existe la ruta indicada\n";
        Inode dir = fs.readInode(sb, dirIdx);
        if (dir.i_type != '0') return "ERROR: rep -> la ruta indicada no es carpeta\n";

        auto entries = fs.listDirectory(sb, dirIdx);
        out << "name | inode | type | perm\n";
        for (const auto& entry : entries) {
            Inode inode = fs.readInode(sb, entry.inodeIndex);
            out << entry.name << " | " << entry.inodeIndex << " | " << inode.i_type << " | " << inode.i_perm << "\n";
        }
        return finish_ok("ls");
    }

    if (name == "tree") {
        std::function<void(int32_t,const std::string&,int)> walk = [&](int32_t idx, const std::string& label, int depth) {
            Inode inode = fs.readInode(sb, idx);
            for (int i = 0; i < depth; ++i) out << "  ";
            out << label << " (inode=" << idx << ", type=" << inode.i_type << ", perm=" << inode.i_perm << ")\n";
            if (inode.i_type != '0') return;
            auto entries = fs.listDirectory(sb, idx);
            for (const auto& entry : entries) {
                if (entry.name == "." || entry.name == "..") continue;
                walk(entry.inodeIndex, entry.name, depth + 1);
            }
        };
        walk(0, "/", 0);
        return finish_ok("tree");
    }

    return "ERROR: rep -> nombre de reporte no soportado\n";
}
