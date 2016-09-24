MOUNT_POINT = '/home/vagrant/rdfs'

Vagrant.configure("2") do |config|
    config.vm.box = "bento/ubuntu-16.04"

    config.vm.provider "virtualbox" do |v|
        v.name = "RDFS_VM"

        # Give 1 gb of ram to the vm, may change if it's not enough
        v.customize ["modifyvm", :id, "--memory", 1024]
        v.customize ["setextradata", :id,
            "VBoxInternal2/SharedFoldersEnableSymlinksCreate//home/vagrant/rdfs", "1"]
    end

    # Add to /etc/hosts: 33.33.33.33 rdfs
    config.vm.network "private_network", ip: "33.33.33.33"
    # TODO: do we need to forward any ports?
    # config.vm.network "forwarded_port", guest: 80, host: 8000

    # Mount the current folder to /home/vagrant/rdfs.
    config.vm.synced_folder ".", MOUNT_POINT

    config.vm.provision "shell", path: "vagrant_provision.sh"
end
