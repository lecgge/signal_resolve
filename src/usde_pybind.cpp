// usde_pybind.cpp — Python extension module for USDE
//
// Build: see CMakeLists.txt (target usde_python)
// Usage: import usde_python

#include "usde_types.h"
#include "dbc_parser.h"
#include "ldf_parser.h"
#include "arxml_parser.h"
#include "codec_engine.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>          // automatic vector <-> list
#include <filesystem>

namespace py = pybind11;

// ─── Helper: convert DecodedSignal -> Python dict ───────────────────────────

static py::dict DecodedSignalToDict(const usde::DecodedSignal& ds) {
    py::dict d;
    d["name"]  = ds.name;
    d["value"] = ds.physical_value;
    d["unit"]  = ds.unit;
    return d;
}

static py::list DecodeToPyList(const std::vector<usde::DecodedSignal>& vec) {
    py::list lst;
    for (auto& ds : vec)
        lst.append(DecodedSignalToDict(ds));
    return lst;
}

// ─── NetworkCluster wrapper ─────────────────────────────────────────────────

class PyNetwork {
public:
    usde::NetworkCluster cluster;

    bool load_dbc(const std::string& path) {
        return usde::LoadDBC(path, cluster);
    }

    bool load_ldf(const std::string& path) {
        return usde::LoadLDF(path, cluster);
    }

    bool load_arxml(const std::string& path) {
        return usde::LoadARXML(path, cluster);
    }

    size_t frame_count() const {
        return cluster.frames.size();
    }

    py::list decode_frame(uint32_t frame_id,
                          const std::vector<uint8_t>& raw_bytes) const {
        auto it = cluster.frames.find(frame_id);
        if (it == cluster.frames.end())
            throw py::key_error("Frame ID not found: " + std::to_string(frame_id));

        auto decoded = usde::CodecEngine::DecodeFrame(
            it->second, raw_bytes.data(), raw_bytes.size());
        return DecodeToPyList(decoded);
    }

    std::vector<uint8_t> encode_frame(
        uint32_t frame_id,
        const std::unordered_map<std::string, double>& values) const
    {
        auto it = cluster.frames.find(frame_id);
        if (it == cluster.frames.end())
            throw py::key_error("Frame ID not found: " + std::to_string(frame_id));

        // Use DLC as buffer size
        size_t buf_size = it->second.dlc;
        if (buf_size == 0) buf_size = 8;
        std::vector<uint8_t> buf(buf_size, 0);

        usde::CodecEngine::EncodeFrame(it->second, values,
                                       buf.data(), buf.size());
        return buf;
    }

    py::dict frame_info(uint32_t frame_id) const {
        auto it = cluster.frames.find(frame_id);
        if (it == cluster.frames.end())
            throw py::key_error("Frame ID not found: " + std::to_string(frame_id));

        const auto& f = it->second;
        py::dict d;
        d["id"]   = f.id;
        d["name"] = f.name;
        d["dlc"]  = f.dlc;

        py::list sigs;
        for (auto& s : f.signals) {
            py::dict sd;
            sd["name"]       = s.name;
            sd["start_bit"]  = s.start_bit;
            sd["bit_length"] = s.bit_length;
            sd["byte_order"] = (s.byte_order == usde::ByteOrder::INTEL)
                                    ? "Intel" : "Motorola";
            sd["factor"]     = s.factor;
            sd["offset"]     = s.offset;
            sd["min_value"]  = s.min_value;
            sd["max_value"]  = s.max_value;
            sd["unit"]       = s.unit;
            sigs.append(sd);
        }
        d["signals"] = sigs;

        py::list pdu_list;
        for (auto& p : f.pdus) {
            py::dict pd;
            pd["name"] = p.name;
            pd["byte_length"] = p.byte_length;
            py::list psigs;
            for (auto& s : p.signals) {
                py::dict sd;
                sd["name"]       = s.name;
                sd["start_bit"]  = s.start_bit;
                sd["bit_length"] = s.bit_length;
                sd["byte_order"] = (s.byte_order == usde::ByteOrder::INTEL)
                                        ? "Intel" : "Motorola";
                psigs.append(sd);
            }
            pd["signals"] = psigs;
            pdu_list.append(pd);
        }
        d["pdus"] = pdu_list;
        return d;
    }

    py::list cluster_list() const {
        py::list result;
        for (auto& [name, cl] : cluster.clusters) {
            py::dict cd;
            cd["name"]     = cl.name;
            cd["bus_type"] = cl.bus_type;
            cd["baudrate"] = cl.baudrate;
            cd["can_fd"]   = cl.can_fd;
            py::list fids;
            for (auto& [fid, _] : cl.frames) fids.append(fid);
            cd["frame_ids"] = fids;
            result.append(cd);
        }
        return result;
    }
};

// ─── Module definition ──────────────────────────────────────────────────────

PYBIND11_MODULE(usde_python, m) {
    m.doc() = "USDE — Universal Signal Decoding Engine (Python binding)";

    py::class_<PyNetwork>(m, "Network")
        .def(py::init<>())
        .def("load_dbc",   &PyNetwork::load_dbc,
             py::arg("path"),
             "Parse a DBC file into the network. Returns True on success.")
        .def("load_ldf",   &PyNetwork::load_ldf,
             py::arg("path"),
             "Parse a LDF file into the network. Returns True on success.")
        .def("load_arxml", &PyNetwork::load_arxml,
             py::arg("path"),
             "Parse an ARXML file into the network. Returns True on success.")
        .def("frame_count", &PyNetwork::frame_count,
             "Number of loaded frames.")
        .def("decode_frame", &PyNetwork::decode_frame,
             py::arg("frame_id"), py::arg("raw_bytes"),
             "Decode a frame. Returns list of dicts: [{name, value, unit}, ...]")
        .def("encode_frame", &PyNetwork::encode_frame,
             py::arg("frame_id"), py::arg("values"),
             "Encode a frame from a {name: value} dict. Returns bytes.")
        .def("frame_info", &PyNetwork::frame_info,
             py::arg("frame_id"),
             "Get frame metadata (id, name, dlc, signals, pdus, clusters).")
        .def("clusters", &PyNetwork::cluster_list,
             "Get list of cluster dicts [{name, bus_type, baudrate, can_fd, frame_ids}].");
}
