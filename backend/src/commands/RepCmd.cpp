#include "commands/RepCmd.h"

#include "disk/BinaryIO.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/FsFileOps.h"
#include "structs/EBR.h"
#include "structs/MBR.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    std::string tolower_str(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string strip_quotes(std::string v) {
        v = trim(v);
        if (v.size() >= 2) {
            if ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')) {
                return v.substr(1, v.size() - 2);
            }
        }
        return v;
    }

    std::unordered_map<std::string, std::string> parse_params_local(const std::string& line) {
        std::unordered_map<std::string, std::string> p;
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        while (iss >> token) {
            if (token.rfind("-", 0) != 0) continue;
            auto eq = token.find('=');
            if (eq == std::string::npos) {
                p[tolower_str(token.substr(1))] = "true";
            } else {
                std::string key = tolower_str(token.substr(1, eq - 1));
                std::string val = strip_quotes(token.substr(eq + 1));
                p[key] = val;
            }
        }
        return p;
    }

    std::string part_name_to_string(const char part_name[16]) {
        char tmp[17];
        std::memset(tmp, 0, sizeof(tmp));
        std::memcpy(tmp, part_name, 16);
        return std::string(tmp);
    }

    bool is_used_part(const Partition& p) {
        return p.part_status == '1' && p.part_start >= 0 && p.part_size > 0;
    }

    bool find_extended_part(const MBR& mbr, Partition& out) {
        for (int i = 0; i < 4; i++) {
            const auto& p = mbr.mbr_partitions[i];
            if (is_used_part(p) && (p.part_type == 'E' || p.part_type == 'e')) {
                out = p;
                return true;
            }
        }
        return false;
    }

    std::string html_escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\n': out += "<BR/>"; break;
                default: out.push_back(c); break;
            }
        }
        return out;
    }

    std::string shell_escape(const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out.push_back(c);
        }
        out += "'";
        return out;
    }

    std::string join_blocks(const Inode& inode) {
        std::ostringstream out;
        bool first = true;
        for (int i = 0; i < 15; ++i) {
            if (!first) out << ", ";
            out << inode.i_block[i];
            first = false;
        }
        return out.str();
    }

    std::string file_block_content(const FileBlock& fb) {
        std::string content;
        for (char c : fb.b_content) {
            if (c == '\0') break;
            content.push_back(c);
        }
        return content;
    }

    bool write_text_file(const std::string& path, const std::string& content) {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << content;
        return true;
    }

    bool render_dot(const std::string& dot, const std::string& out_path, std::string& error) {
        std::filesystem::path output(out_path);
        const std::string ext = tolower_str(output.extension().string());

        if (ext == ".dot") {
            if (!write_text_file(out_path, dot)) {
                error = "no se pudo escribir el archivo .dot";
                return false;
            }
            return true;
        }

        const std::unordered_map<std::string, std::string> format_map = {
            {".png", "png"},
            {".jpg", "jpg"},
            {".jpeg", "jpg"},
            {".svg", "svg"},
            {".pdf", "pdf"}
        };

        auto it = format_map.find(ext);
        if (it == format_map.end()) {
            error = "extensión de salida no soportada para Graphviz";
            return false;
        }

        std::filesystem::path dot_path = output;
        dot_path.replace_extension(".dot");
        if (!write_text_file(dot_path.string(), dot)) {
            error = "no se pudo escribir el archivo temporal .dot";
            return false;
        }

        std::string cmd = "dot -T" + it->second + " "
            + shell_escape(dot_path.string())
            + " -o " + shell_escape(out_path);

        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            error = "falló la ejecución de Graphviz";
            return false;
        }
        return true;
    }

    std::string build_mbr_dot(const MBR& mbr, const std::string& disk_path, const std::string& used_id) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  graph [pad=0.2, nodesep=0.4, ranksep=0.4];\n";
        dot << "  node [shape=plain];\n";
        dot << "  mbr [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "      <TR><TD BGCOLOR=\"lightsteelblue\" COLSPAN=\"2\"><B>REPORTE MBR</B></TD></TR>\n";
        dot << "      <TR><TD><B>DISK</B></TD><TD>" << html_escape(disk_path) << "</TD></TR>\n";
        if (!used_id.empty()) {
            dot << "      <TR><TD><B>ID</B></TD><TD>" << html_escape(used_id) << "</TD></TR>\n";
        }
        dot << "      <TR><TD><B>mbr_tamano</B></TD><TD>" << mbr.mbr_tamano << "</TD></TR>\n";
        dot << "      <TR><TD><B>mbr_fecha_creacion</B></TD><TD>" << mbr.mbr_fecha_creacion << "</TD></TR>\n";
        dot << "      <TR><TD><B>mbr_disk_signature</B></TD><TD>" << mbr.mbr_disk_signature << "</TD></TR>\n";
        dot << "      <TR><TD><B>mbr_fit</B></TD><TD>" << mbr.mbr_fit << "</TD></TR>\n";
        for (int i = 0; i < 4; ++i) {
            const auto& pt = mbr.mbr_partitions[i];
            dot << "      <TR><TD BGCOLOR=\"lightgray\" COLSPAN=\"2\"><B>Particion " << i << "</B></TD></TR>\n";
            dot << "      <TR><TD>status</TD><TD>" << pt.part_status << "</TD></TR>\n";
            dot << "      <TR><TD>type</TD><TD>" << pt.part_type << "</TD></TR>\n";
            dot << "      <TR><TD>fit</TD><TD>" << pt.part_fit << "</TD></TR>\n";
            dot << "      <TR><TD>start</TD><TD>" << pt.part_start << "</TD></TR>\n";
            dot << "      <TR><TD>size</TD><TD>" << pt.part_size << "</TD></TR>\n";
            dot << "      <TR><TD>name</TD><TD>" << html_escape(part_name_to_string(pt.part_name)) << "</TD></TR>\n";
        }
        dot << "    </TABLE>\n";
        dot << "  >];\n";
        dot << "}\n";
        return dot.str();
    }

    std::string build_disk_dot(const MBR& mbr) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  graph [pad=0.2];\n";
        dot << "  node [shape=record];\n";
        dot << "  disk [label=\"MBR";
        for (int i = 0; i < 4; ++i) {
            const auto& pt = mbr.mbr_partitions[i];
            if (!is_used_part(pt)) continue;
            dot << "|{" << html_escape(part_name_to_string(pt.part_name))
                << "|type=" << pt.part_type
                << "|start=" << pt.part_start
                << "|size=" << pt.part_size << "}";
        }
        dot << "\"];\n";
        dot << "}\n";
        return dot.str();
    }

    std::string build_sb_dot(const SuperBlock& sb) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  node [shape=plain];\n";
        dot << "  sb [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "      <TR><TD BGCOLOR=\"lightgoldenrod1\" COLSPAN=\"2\"><B>SUPERBLOCK</B></TD></TR>\n";
        auto row = [&](const std::string& k, auto v) {
            dot << "      <TR><TD><B>" << k << "</B></TD><TD>" << v << "</TD></TR>\n";
        };
        row("s_filesystem_type", sb.s_filesystem_type);
        row("s_inodes_count", sb.s_inodes_count);
        row("s_blocks_count", sb.s_blocks_count);
        row("s_free_blocks_count", sb.s_free_blocks_count);
        row("s_free_inodes_count", sb.s_free_inodes_count);
        row("s_mtime", sb.s_mtime);
        row("s_umtime", sb.s_umtime);
        row("s_mnt_count", sb.s_mnt_count);
        row("s_magic", sb.s_magic);
        row("s_inode_size", sb.s_inode_size);
        row("s_block_size", sb.s_block_size);
        row("s_first_ino", sb.s_first_ino);
        row("s_first_blo", sb.s_first_blo);
        row("s_bm_inode_start", sb.s_bm_inode_start);
        row("s_bm_block_start", sb.s_bm_block_start);
        row("s_inode_start", sb.s_inode_start);
        row("s_block_start", sb.s_block_start);
        dot << "    </TABLE>\n";
        dot << "  >];\n";
        dot << "}\n";
        return dot.str();
    }

    std::string build_inode_dot(Ext2Paths& fs, const SuperBlock& sb, const std::string& disk_path) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  rankdir=LR;\n";
        dot << "  node [shape=plain];\n";
        for (int i = 0; i < sb.s_inodes_count; ++i) {
            char v = '\0';
            BinaryIO::readAt(disk_path, sb.s_bm_inode_start + i, &v, 1);
            if (v != '1') continue;
            Inode inode = fs.readInode(sb, i);
            dot << "  inode" << i << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "      <TR><TD BGCOLOR=\"lightskyblue\" COLSPAN=\"2\"><B>Inodo " << i << "</B></TD></TR>\n";
            dot << "      <TR><TD>uid</TD><TD>" << inode.i_uid << "</TD></TR>\n";
            dot << "      <TR><TD>gid</TD><TD>" << inode.i_gid << "</TD></TR>\n";
            dot << "      <TR><TD>size</TD><TD>" << inode.i_size << "</TD></TR>\n";
            dot << "      <TR><TD>type</TD><TD>" << inode.i_type << "</TD></TR>\n";
            dot << "      <TR><TD>perm</TD><TD>" << inode.i_perm << "</TD></TR>\n";
            dot << "      <TR><TD>blocks</TD><TD>" << html_escape(join_blocks(inode)) << "</TD></TR>\n";
            dot << "    </TABLE>\n";
            dot << "  >];\n";
        }
        dot << "}\n";
        return dot.str();
    }

    std::string build_block_dot(Ext2Paths& fs, const SuperBlock& sb, const std::string& disk_path) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  rankdir=LR;\n";
        dot << "  node [shape=plain];\n";
        for (int i = 0; i < sb.s_blocks_count; ++i) {
            char v = '\0';
            BinaryIO::readAt(disk_path, sb.s_bm_block_start + i, &v, 1);
            if (v != '1') continue;
            FileBlock fb = fs.readFileBlock(sb, i);
            dot << "  block" << i << " [label=<\n";
            dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
            dot << "      <TR><TD BGCOLOR=\"palegreen\" COLSPAN=\"1\"><B>Bloque " << i << "</B></TD></TR>\n";
            dot << "      <TR><TD>" << html_escape(file_block_content(fb)) << "</TD></TR>\n";
            dot << "    </TABLE>\n";
            dot << "  >];\n";
        }
        dot << "}\n";
        return dot.str();
    }

    std::string build_tree_dot(Ext2Paths& fs, const SuperBlock& sb) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  rankdir=TB;\n";
        dot << "  node [shape=box, style=rounded];\n";

        std::function<void(int32_t, const std::string&)> walk = [&](int32_t idx, const std::string& nodeName) {
            Inode inode = fs.readInode(sb, idx);
            dot << "  " << nodeName << " [label=\"" << html_escape(nodeName)
                << "\\ninode=" << idx
                << "\\ntype=" << inode.i_type
                << "\\nperm=" << inode.i_perm << "\"];\n";

            if (inode.i_type != '0') return;
            auto entries = fs.listDirectory(sb, idx);
            int childNum = 0;
            for (const auto& entry : entries) {
                if (entry.name == "." || entry.name == "..") continue;
                std::string childNode = nodeName + "_" + std::to_string(childNum++);
                walk(entry.inodeIndex, childNode);
                dot << "  " << nodeName << " -> " << childNode
                    << " [label=\"" << html_escape(entry.name) << "\"];\n";
            }
        };

        walk(0, "root");
        dot << "}\n";
        return dot.str();
    }

    std::string build_file_dot(const std::string& path, const std::string& content) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  node [shape=plain];\n";
        dot << "  file [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "      <TR><TD BGCOLOR=\"mistyrose\" COLSPAN=\"1\"><B>" << html_escape(path) << "</B></TD></TR>\n";
        dot << "      <TR><TD>" << html_escape(content) << "</TD></TR>\n";
        dot << "    </TABLE>\n";
        dot << "  >];\n";
        dot << "}\n";
        return dot.str();
    }

    std::string build_ls_dot(Ext2Paths& fs, const SuperBlock& sb, int32_t dirIdx, const std::string& path) {
        std::ostringstream dot;
        dot << "digraph G {\n";
        dot << "  node [shape=plain];\n";
        dot << "  ls [label=<\n";
        dot << "    <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n";
        dot << "      <TR><TD BGCOLOR=\"khaki\" COLSPAN=\"4\"><B>LS " << html_escape(path) << "</B></TD></TR>\n";
        dot << "      <TR><TD><B>Nombre</B></TD><TD><B>Inodo</B></TD><TD><B>Tipo</B></TD><TD><B>Perm</B></TD></TR>\n";
        auto entries = fs.listDirectory(sb, dirIdx);
        for (const auto& entry : entries) {
            Inode inode = fs.readInode(sb, entry.inodeIndex);
            dot << "      <TR><TD>" << html_escape(entry.name) << "</TD><TD>"
                << entry.inodeIndex << "</TD><TD>"
                << inode.i_type << "</TD><TD>"
                << inode.i_perm << "</TD></TR>\n";
        }
        dot << "    </TABLE>\n";
        dot << "  >];\n";
        dot << "}\n";
        return dot.str();
    }
}

std::string RepCmd::exec(const std::string& line) {
    auto params = parse_params_local(line);
    if (!params.count("name")) return "ERROR: rep -> falta -name\n";
    if (!params.count("path")) return "ERROR: rep -> falta -path (salida)\n";

    const std::string name = tolower_str(params["name"]);
    const std::string out_path = params["path"];

    std::string disk_path;
    std::string used_id;
    MountEntry mounted{};

    if (params.count("id")) {
        used_id = params["id"];
        if (!MountManager::instance().getById(used_id, mounted)) {
            return "ERROR: rep -> id no encontrado (use mounted para ver ids)\n";
        }
        disk_path = mounted.path;
    } else if (params.count("disk")) {
        disk_path = params["disk"];
    } else {
        return "ERROR: rep -> falta -id (partición montada) o -disk (disco fuente)\n";
    }

    std::filesystem::path output_path(out_path);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    auto finish_ok = [&](const std::string& kind) {
        std::ostringstream msg;
        msg << "OK: rep -> " << kind << " generado en: " << out_path << "\n";
        return msg.str();
    };

    MBR mbr{};
    if (!BinaryIO::readAt0(disk_path, &mbr, sizeof(MBR))) {
        return "ERROR: rep -> no se pudo leer el MBR del disco\n";
    }

    if (name == "mbr") {
        std::string error;
        if (!render_dot(build_mbr_dot(mbr, disk_path, used_id), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("mbr");
    }

    if (name == "disk") {
        std::string error;
        if (!render_dot(build_disk_dot(mbr), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("disk");
    }

    if (used_id.empty()) {
        return "ERROR: rep -> estos reportes requieren -id\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (sb.s_magic != 0xEF53) return "ERROR: rep -> la partición no está formateada como EXT2/EXT3\n";

    if (name == "bm_inode" || name == "bm_block") {
        int count = (name == "bm_inode") ? sb.s_inodes_count : sb.s_blocks_count;
        int64_t start = (name == "bm_inode") ? sb.s_bm_inode_start : sb.s_bm_block_start;
        std::ostringstream text;
        for (int i = 0; i < count; ++i) {
            char v = '\0';
            BinaryIO::readAt(mounted.path, start + i, &v, 1);
            text << v;
            if ((i + 1) % 32 == 0) text << "\n";
            else text << " ";
        }
        text << "\n";
        if (!write_text_file(out_path, text.str())) {
            return "ERROR: rep -> no se pudo crear el archivo de reporte\n";
        }
        return finish_ok(name);
    }

    if (name == "sb") {
        std::string error;
        if (!render_dot(build_sb_dot(sb), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("sb");
    }

    if (name == "inode") {
        std::string error;
        if (!render_dot(build_inode_dot(fs, sb, mounted.path), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("inode");
    }

    if (name == "block") {
        std::string error;
        if (!render_dot(build_block_dot(fs, sb, mounted.path), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("block");
    }

    if (name == "tree") {
        std::string error;
        if (!render_dot(build_tree_dot(fs, sb), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("tree");
    }

    if (name == "file") {
        if (!params.count("path_file_ls")) return "ERROR: rep -> falta -path_file_ls\n";
        std::string content;
        if (!FsFileOps::readFile(fs, sb, params["path_file_ls"], content, nullptr)) {
            return "ERROR: rep -> no se pudo leer el archivo indicado\n";
        }
        std::string error;
        if (!render_dot(build_file_dot(params["path_file_ls"], content), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("file");
    }

    if (name == "ls") {
        if (!params.count("path_file_ls")) return "ERROR: rep -> falta -path_file_ls\n";
        int32_t dirIdx = fs.resolvePath(sb, params["path_file_ls"]);
        if (dirIdx < 0) return "ERROR: rep -> no existe la ruta indicada\n";
        Inode dir = fs.readInode(sb, dirIdx);
        if (dir.i_type != '0') return "ERROR: rep -> la ruta indicada no es carpeta\n";

        std::string error;
        if (!render_dot(build_ls_dot(fs, sb, dirIdx, params["path_file_ls"]), out_path, error)) {
            return "ERROR: rep -> " + error + "\n";
        }
        return finish_ok("ls");
    }

    return "ERROR: rep -> nombre de reporte no soportado\n";
}
