#include "pch.h"
#include "PreviewApplication.h"

namespace
{
    using namespace winrt;
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Markup;

    using XamlControlsProvider = winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider;

    struct PreviewApplication : ApplicationT<PreviewApplication, IXamlMetadataProvider>
    {
        IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type)
        {
            return Provider().GetXamlType(type);
        }

        IXamlType GetXamlType(hstring const& fullName)
        {
            return Provider().GetXamlType(fullName);
        }

        com_array<XmlnsDefinition> GetXmlnsDefinitions()
        {
            return Provider().GetXmlnsDefinitions();
        }

    private:
        XamlControlsProvider Provider()
        {
            if (!m_provider)
            {
                m_provider = XamlControlsProvider{};
            }
            return m_provider;
        }

        XamlControlsProvider m_provider{ nullptr };
    };
}

namespace PlaylistPreview::Shell
{
    winrt::Microsoft::UI::Xaml::Application CreatePreviewApplication()
    {
        return winrt::make<PreviewApplication>();
    }
}
