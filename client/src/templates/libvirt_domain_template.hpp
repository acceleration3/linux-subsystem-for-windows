#pragma once

#include <string>

namespace templates {

const std::string LIBVIRT_DOMAIN_TEMPLATE = R"(<domain type="kvm">
  <name>{{VM_NAME}}</name>
  <uuid>{{VM_UUID}}</uuid>
  <metadata>
    <libosinfo:libosinfo xmlns:libosinfo="http://libosinfo.org/xmlns/libvirt/domain/1.0">
      <libosinfo:os id="http://microsoft.com/win/11"/>
    </libosinfo:libosinfo>
  </metadata>
  <memory unit="KiB">{{MEMORY_KB}}</memory>
  <currentMemory unit="KiB">{{MEMORY_KB}}</currentMemory>
  <vcpu placement="static">{{CPU_COUNT}}</vcpu>

  <!-- BIOS boot (no UEFI loader) -->
  <os>
    <type arch="x86_64" machine="pc-q35-10.1">hvm</type>
    <boot dev="cdrom"/>
    <boot dev="hd"/>
  </os>

  <features>
    <acpi/>
    <apic/>
    <hyperv mode="custom">
      <relaxed state="on"/>
      <vapic state="on"/>
      <spinlocks state="on" retries="8191"/>
      <vpindex state="on"/>
      <runtime state="on"/>
      <synic state="on"/>
      <stimer state="on"/>
      <frequencies state="on"/>
      <tlbflush state="on"/>
      <ipi state="on"/>
      <avic state="on"/>
    </hyperv>
    <vmport state="off"/>
    <smm state="on"/>
  </features>

  <cpu mode="host-passthrough" check="none" migratable="on"/>
  <clock offset="localtime">
    <timer name="rtc" tickpolicy="catchup"/>
    <timer name="pit" tickpolicy="delay"/>
    <timer name="hpet" present="no"/>
    <timer name="hypervclock" present="yes"/>
  </clock>

  <on_poweroff>destroy</on_poweroff>
  <on_reboot>restart</on_reboot>
  <on_crash>destroy</on_crash>

  <devices>
    <emulator>/usr/bin/qemu-system-x86_64</emulator>

    <!-- System disk -->
    <disk type="file" device="disk">
      <driver name="qemu" type="qcow2" discard="unmap"/>
      <source file="{{DISK_PATH}}"/>
      <target dev="vda" bus="virtio"/>
    </disk>

    <!-- Windows installer ISO (bootable) -->
    <disk type="file" device="cdrom">
      <driver name="qemu" type="raw"/>
      <source file="{{WINDOWS_ISO_PATH}}"/>
      <target dev="sdb" bus="sata"/>
      <readonly/>
    </disk>

    <!-- Autounattend ISO -->
    <disk type="file" device="cdrom">
      <driver name="qemu" type="raw"/>
      <source file="{{AUTOUNATTEND_ISO_PATH}}"/>
      <target dev="sdc" bus="sata"/>
      <readonly/>
    </disk>

    <!-- VirtIO Guest Tools ISO -->
    <disk type="file" device="cdrom">
      <driver name="qemu" type="raw"/>
      <source file="{{VIRTIO_ISO_PATH}}"/>
      <target dev="sdd" bus="sata"/>
      <readonly/>
    </disk>

    <!-- Controllers -->
    <controller type="usb" index="0" model="qemu-xhci" ports="15"/>
    <controller type="pci" index="0" model="pcie-root"/>
    <controller type="pci" index="1" model="pcie-root-port"/>
    <controller type="pci" index="2" model="pcie-root-port"/>
    <controller type="pci" index="3" model="pcie-root-port"/>
    <controller type="pci" index="4" model="pcie-root-port"/>
    <controller type="pci" index="5" model="pcie-root-port"/>
    <controller type="pci" index="6" model="pcie-root-port"/>
    <controller type="sata" index="0"/>
    <controller type="virtio-serial" index="0"/>

    <!-- Network -->
    <interface type="network">
      <mac address="{{MAC_ADDRESS}}"/>
      <source network="{{NETWORK_NAME}}"/>
      <model type="virtio"/>
    </interface>

    <!-- Console -->
    <serial type="pty"/>
    <console type="pty"/>
    <channel type="spicevmc">
      <target type="virtio" name="com.redhat.spice.0"/>
    </channel>

    <!-- Input -->
    <input type="tablet" bus="usb"/>
    <input type="mouse" bus="ps2"/>
    <input type="keyboard" bus="ps2"/>

    <!-- Graphics -->
    <graphics type="spice">
      <listen type="none"/>
      <image compression="off"/>
      <gl enable="yes" rendernode="{{RENDER_NODE}}"/>
    </graphics>

    <!-- Sound -->
    <sound model="ich9"/>
    <audio id="1" type="spice"/>

    <!-- Video -->
    <video>
      <model type="virtio" heads="1" primary="yes">
        <acceleration accel3d="yes"/>
      </model>
    </video>

    <!-- Misc -->
    <redirdev bus="usb" type="spicevmc"/>
    <redirdev bus="usb" type="spicevmc"/>
    <watchdog model="itco" action="reset"/>
    <memballoon model="virtio"/>
  </devices>
</domain>)";

// Template variable placeholders:
// {{VM_NAME}} - Virtual machine name (e.g., "LSWVM")
// {{VM_UUID}} - Virtual machine UUID (e.g., "87f5a75e-65ad-4046-ad96-3a8d63326f58")
// {{MEMORY_KB}} - Memory in KiB (e.g., "4194304" for 4GB)
// {{CPU_COUNT}} - Number of CPU cores (e.g., "4")
// {{DISK_PATH}} - Path to the VM disk image (e.g., "/var/lib/libvirt/images/LSWVM.qcow2")
// {{WINDOWS_ISO_PATH}} - Path to Windows installer ISO
// {{AUTOUNATTEND_ISO_PATH}} - Path to autounattend ISO (e.g., "/tmp/autounattend.iso")
// {{VIRTIO_ISO_PATH}} - Path to VirtIO guest tools ISO (e.g.,
// "/usr/share/virtio-win/virtio-win.iso")
// {{MAC_ADDRESS}} - MAC address for network interface (e.g., "52:54:00:20:cd:e9")
// {{NETWORK_NAME}} - Libvirt network name (e.g., "default")
// {{RENDER_NODE}} - GPU render node path (e.g., "/dev/dri/by-path/pci-0000:03:00.0-render")

} // namespace templates
